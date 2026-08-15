// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

std::string json_escape(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 16);
  for (const unsigned char c : value) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20) {
          std::ostringstream escaped;
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
          result += escaped.str();
        } else result.push_back(static_cast<char>(c));
    }
  }
  return result;
}

std::optional<std::string> json_string_field(std::string_view body, std::string_view field,
                                             std::size_t start = 0) {
  const auto key = std::string("\"") + std::string(field) + "\"";
  const auto key_pos = body.find(key, start);
  if (key_pos == std::string_view::npos) return std::nullopt;
  const auto colon = body.find(':', key_pos + key.size());
  if (colon == std::string_view::npos) return std::nullopt;
  auto quote = body.find('"', colon + 1);
  if (quote == std::string_view::npos) return std::nullopt;
  std::string result;
  bool escaped = false;
  for (std::size_t index = quote + 1; index < body.size(); ++index) {
    const char c = body[index];
    if (escaped) {
      switch (c) {
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: result.push_back(c); break;
      }
      escaped = false;
    } else if (c == '\\') escaped = true;
    else if (c == '"') return result;
    else result.push_back(c);
  }
  return std::nullopt;
}

std::optional<std::int64_t> json_integer_field(std::string_view body, std::string_view field,
                                               std::size_t start = 0) {
  const auto key = std::string("\"") + std::string(field) + "\"";
  const auto key_pos = body.find(key, start);
  if (key_pos == std::string_view::npos) return std::nullopt;
  const auto colon = body.find(':', key_pos + key.size());
  if (colon == std::string_view::npos) return std::nullopt;
  auto begin = body.find_first_not_of(" \t\r\n", colon + 1);
  if (begin == std::string_view::npos) return std::nullopt;
  auto end = begin;
  if (body[end] == '-') ++end;
  while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end]))) ++end;
  if (end == begin || (end == begin + 1 && body[begin] == '-')) return std::nullopt;
  try { return std::stoll(std::string(body.substr(begin, end - begin))); }
  catch (...) { return std::nullopt; }
}

struct LspRequestPosition final { std::uint32_t line = 0; std::uint32_t character = 0; };

LspRequestPosition lsp_request_position(std::string_view body) {
  const auto position = body.find("\"position\"");
  if (position == std::string_view::npos) return {};
  const auto line = json_integer_field(body, "line", position).value_or(0);
  const auto character = json_integer_field(body, "character", position).value_or(0);
  return {static_cast<std::uint32_t>(std::max<std::int64_t>(0, line)),
          static_cast<std::uint32_t>(std::max<std::int64_t>(0, character))};
}

std::uint32_t lsp_byte_offset(std::string_view text, LspRequestPosition position) {
  std::size_t line_start = 0;
  for (std::uint32_t line = 0; line < position.line; ++line) {
    const auto newline = text.find('\n', line_start);
    if (newline == std::string_view::npos) return static_cast<std::uint32_t>(text.size());
    line_start = newline + 1;
  }
  std::uint32_t utf16 = 0;
  std::size_t offset = line_start;
  while (offset < text.size() && text[offset] != '\n' && utf16 < position.character) {
    const auto lead = static_cast<unsigned char>(text[offset]);
    std::uint32_t codepoint = lead;
    std::size_t width = 1;
    if ((lead & 0xE0U) == 0xC0U && offset + 1 < text.size()) {
      codepoint = ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(text[offset + 1]) & 0x3FU); width = 2;
    } else if ((lead & 0xF0U) == 0xE0U && offset + 2 < text.size()) {
      codepoint = ((lead & 0x0FU) << 12U) | ((static_cast<unsigned char>(text[offset + 1]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[offset + 2]) & 0x3FU); width = 3;
    } else if ((lead & 0xF8U) == 0xF0U && offset + 3 < text.size()) {
      codepoint = ((lead & 0x07U) << 18U) | ((static_cast<unsigned char>(text[offset + 1]) & 0x3FU) << 12U) |
                  ((static_cast<unsigned char>(text[offset + 2]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[offset + 3]) & 0x3FU); width = 4;
    }
    const std::uint32_t units = codepoint > 0xFFFFU ? 2U : 1U;
    if (utf16 + units > position.character) break;
    utf16 += units; offset += width;
  }
  return static_cast<std::uint32_t>(offset);
}

std::filesystem::path lsp_virtual_path(std::string_view uri) {
  constexpr std::string_view prefix = "file://";
  if (uri.rfind(prefix, 0) == 0) return std::filesystem::path(std::string(uri.substr(prefix.size())));
  return std::filesystem::path(std::string(uri));
}

raz::compiler::FrontendAnalysis lsp_analysis(std::string_view uri, std::string_view text) {
  return Compiler{}.analyze_text(lsp_virtual_path(uri), std::string(text));
}

std::string lsp_diagnostics(std::string_view uri, std::string_view text) {
  auto analysis = lsp_analysis(uri, text);
  return analysis.diagnostics.lsp_json(uri, analysis.sources);
}

std::string lsp_quick_fixes(std::string_view uri, std::string_view text) {
  auto analysis = lsp_analysis(uri, text);
  return analysis.diagnostics.lsp_code_actions_json(uri, analysis.sources);
}

std::string lsp_range_json(const raz::compiler::SourceManager& sources, raz::compiler::SourceRange range) {
  const auto start = sources.lsp_position(range.begin);
  const auto end = sources.lsp_position(range.end);
  return "{\"start\":{\"line\":" + std::to_string(start.line) + ",\"character\":" + std::to_string(start.character) +
         "},\"end\":{\"line\":" + std::to_string(end.line) + ",\"character\":" + std::to_string(end.character) + "}}";
}

int lsp_symbol_kind(raz::compiler::SemanticSymbolKind kind) {
  using K = raz::compiler::SemanticSymbolKind;
  switch (kind) {
    case K::namespace_: return 3;
    case K::type: return 23;
    case K::enum_: return 10;
    case K::trait: return 11;
    case K::function: case K::method: return 12;
    case K::field: return 8;
    case K::parameter: case K::variable: return 13;
    case K::constant: return 14;
    case K::enum_variant: return 22;
  }
  return 13;
}

std::string semantic_kind_name(raz::compiler::SemanticSymbolKind kind) {
  using K = raz::compiler::SemanticSymbolKind;
  switch (kind) {
    case K::namespace_: return "namespace"; case K::type: return "struct"; case K::enum_: return "enum";
    case K::trait: return "trait"; case K::function: return "function"; case K::method: return "method";
    case K::field: return "field"; case K::parameter: return "parameter"; case K::variable: return "variable";
    case K::constant: return "constant"; case K::enum_variant: return "enum variant";
  }
  return "symbol";
}

const raz::compiler::SemanticOccurrence* lsp_occurrence_at(const raz::compiler::FrontendAnalysis& analysis,
                                                            std::uint32_t offset) {
  const raz::compiler::SemanticOccurrence* best = nullptr;
  for (const auto& occurrence : analysis.occurrences) {
    if (!occurrence.range.valid()) continue;
    if (occurrence.range.begin.offset <= offset && offset <= occurrence.range.end.offset) {
      if (best == nullptr || occurrence.range.size() < best->range.size()) best = &occurrence;
    }
  }
  return best;
}

std::string lsp_semantic_hover(const raz::compiler::FrontendAnalysis& analysis, std::uint32_t offset) {
  const auto* occurrence = lsp_occurrence_at(analysis, offset);
  if (occurrence == nullptr || !occurrence->symbol.has_value() || *occurrence->symbol >= analysis.symbols.size()) return "null";
  const auto& symbol = analysis.symbols[*occurrence->symbol];
  std::string markdown = "```raz\\n" + symbol.detail + "\\n```\\n\\n**" + semantic_kind_name(symbol.kind) + "**";
  if (!symbol.container.empty()) markdown += " in `" + symbol.container + "`";
  return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) + "\"},\"range\":" +
         lsp_range_json(analysis.sources, occurrence->range) + "}";
}

std::string lsp_semantic_completion(const std::unordered_map<std::string, raz::compiler::FrontendAnalysis>& analyses,
                                    std::string_view current_uri) {
  struct Item { std::string label; int kind; std::string detail; };
  std::map<std::string, Item> items;
  for (const auto& keyword : {"fn","struct","enum","trait","match","return","async","await","spawn","unsafe","if","while","for","move"})
    items.emplace(keyword, Item{keyword, 14, "keyword"});
  for (const auto& [uri, analysis] : analyses) {
    for (const auto& symbol : analysis.symbols) {
      if (symbol.kind == raz::compiler::SemanticSymbolKind::variable || symbol.kind == raz::compiler::SemanticSymbolKind::parameter) {
        if (uri != current_uri) continue;
      }
      items.insert_or_assign(symbol.name, Item{symbol.name, lsp_symbol_kind(symbol.kind), symbol.detail});
    }
  }
  std::ostringstream out; out << "{\"isIncomplete\":false,\"items\":["; bool first = true;
  for (const auto& [_, item] : items) {
    if (!first) out << ','; first = false;
    out << "{\"label\":\"" << json_escape(item.label) << "\",\"kind\":" << item.kind
        << ",\"detail\":\"" << json_escape(item.detail) << "\"}";
  }
  out << "]}"; return out.str();
}

std::string lsp_semantic_document_symbols(const raz::compiler::FrontendAnalysis& analysis) {
  std::ostringstream out; out << '['; bool first = true;
  for (const auto& symbol : analysis.symbols) {
    if (!symbol.declaration.valid()) continue;
    if (!first) out << ','; first = false;
    out << "{\"name\":\"" << json_escape(symbol.name) << "\",\"detail\":\"" << json_escape(symbol.detail)
        << "\",\"kind\":" << lsp_symbol_kind(symbol.kind) << ",\"range\":" << lsp_range_json(analysis.sources, symbol.declaration)
        << ",\"selectionRange\":" << lsp_range_json(analysis.sources, symbol.declaration) << "}";
  }
  out << ']'; return out.str();
}

bool lsp_global_symbol(raz::compiler::SemanticSymbolKind kind) {
  using K = raz::compiler::SemanticSymbolKind;
  return kind == K::namespace_ || kind == K::type || kind == K::enum_ || kind == K::trait ||
         kind == K::function || kind == K::constant || kind == K::enum_variant;
}

std::string lsp_semantic_definition(const std::unordered_map<std::string, raz::compiler::FrontendAnalysis>& analyses,
                                    std::string_view uri, std::uint32_t offset) {
  const auto current = analyses.find(std::string(uri)); if (current == analyses.end()) return "null";
  const auto* occurrence = lsp_occurrence_at(current->second, offset); if (occurrence == nullptr) return "null";
  if (occurrence->symbol.has_value() && *occurrence->symbol < current->second.symbols.size()) {
    const auto& symbol = current->second.symbols[*occurrence->symbol];
    return "{\"uri\":\"" + json_escape(uri) + "\",\"range\":" + lsp_range_json(current->second.sources, symbol.declaration) + "}";
  }

  for (const auto& [candidate_uri, analysis] : analyses) for (const auto& symbol : analysis.symbols) {
    if (symbol.name == occurrence->name && lsp_global_symbol(symbol.kind))
      return "{\"uri\":\"" + json_escape(candidate_uri) + "\",\"range\":" + lsp_range_json(analysis.sources, symbol.declaration) + "}";
  }
  return "null";
}

std::string lsp_semantic_references(const std::unordered_map<std::string, raz::compiler::FrontendAnalysis>& analyses,
                                    std::string_view uri, std::uint32_t offset, bool include_declaration = true) {
  const auto current = analyses.find(std::string(uri)); if (current == analyses.end()) return "[]";
  const auto* target = lsp_occurrence_at(current->second, offset); if (target == nullptr) return "[]";
  std::string name = target->name; bool global = true; std::optional<std::size_t> local_symbol;
  if (target->symbol.has_value() && *target->symbol < current->second.symbols.size()) {
    const auto& symbol = current->second.symbols[*target->symbol]; global = lsp_global_symbol(symbol.kind);
    if (!global) local_symbol = target->symbol;
  }
  std::ostringstream out; out << '['; bool first = true;
  for (const auto& [candidate_uri, analysis] : analyses) {
    if (!global && candidate_uri != uri) continue;
    for (const auto& occurrence : analysis.occurrences) {
      if (occurrence.name != name || (!include_declaration && occurrence.declaration)) continue;
      if (!global && occurrence.symbol != local_symbol) continue;
      if (!first) out << ','; first = false;
      out << "{\"uri\":\"" << json_escape(candidate_uri) << "\",\"range\":" << lsp_range_json(analysis.sources, occurrence.range) << "}";
    }
  }
  out << ']'; return out.str();
}

std::string lsp_semantic_rename(const std::unordered_map<std::string, raz::compiler::FrontendAnalysis>& analyses,
                                std::string_view uri, std::uint32_t offset, std::string_view replacement) {
  const auto current = analyses.find(std::string(uri)); if (current == analyses.end()) return "null";
  const auto* target = lsp_occurrence_at(current->second, offset); if (target == nullptr || target->name.empty()) return "null";
  bool global = true; std::optional<std::size_t> local_symbol;
  if (target->symbol.has_value() && *target->symbol < current->second.symbols.size()) {
    const auto& symbol = current->second.symbols[*target->symbol]; global = lsp_global_symbol(symbol.kind); if (!global) local_symbol = target->symbol;
  }
  std::ostringstream out; out << "{\"changes\":{"; bool first_doc = true;
  for (const auto& [candidate_uri, analysis] : analyses) {
    if (!global && candidate_uri != uri) continue;
    std::ostringstream edits; bool first_edit = true;
    for (const auto& occurrence : analysis.occurrences) {
      if (occurrence.name != target->name) continue;
      if (!global && occurrence.symbol != local_symbol) continue;
      if (!first_edit) edits << ','; first_edit = false;
      edits << "{\"range\":" << lsp_range_json(analysis.sources, occurrence.range) << ",\"newText\":\"" << json_escape(replacement) << "\"}";
    }

    if (first_edit) continue;
    if (!first_doc) out << ','; first_doc = false;
    out << "\"" << json_escape(candidate_uri) << "\":[" << edits.str() << ']';
  }
  out << "}}"; return out.str();
}

int lsp_semantic_token_type(raz::compiler::SemanticSymbolKind kind) {
  using K = raz::compiler::SemanticSymbolKind;
  switch (kind) {
    case K::function: case K::method: return 1;
    case K::type: case K::enum_: case K::trait: return 2;
    case K::parameter: return 4;
    case K::field: return 5;
    case K::enum_variant: return 6;
    case K::namespace_: return 7;
    default: return 3;
  }
}

std::string lsp_semantic_signature_help(const raz::compiler::FrontendAnalysis& analysis, std::uint32_t offset) {
  const raz::compiler::SemanticSymbol* best = nullptr;
  std::uint32_t best_offset = 0;
  for (const auto& occurrence : analysis.occurrences) {
    if (occurrence.range.end.offset > offset || !occurrence.symbol.has_value() || *occurrence.symbol >= analysis.symbols.size()) continue;
    const auto& symbol = analysis.symbols[*occurrence.symbol];
    if (symbol.kind != raz::compiler::SemanticSymbolKind::function && symbol.kind != raz::compiler::SemanticSymbolKind::method) continue;
    if (best == nullptr || occurrence.range.end.offset >= best_offset) { best = &symbol; best_offset = occurrence.range.end.offset; }
  }

  if (best == nullptr) return "null";
  return "{\"signatures\":[{\"label\":\"" + json_escape(best->detail) + "\"}],\"activeSignature\":0,\"activeParameter\":0}";
}

std::string lsp_semantic_inlay_hints(const raz::compiler::FrontendAnalysis& analysis) {
  std::ostringstream out; out << '['; bool first = true;
  for (const auto& symbol : analysis.symbols) {
    if (symbol.kind != raz::compiler::SemanticSymbolKind::variable || symbol.type_name.empty() || !symbol.declaration.valid()) continue;
    const auto contents = analysis.sources.text(symbol.declaration.begin.file);
    const auto line = analysis.sources.line_column(symbol.declaration.begin);
    const auto* file = analysis.sources.get(symbol.declaration.begin.file); if (file == nullptr || line.line == 0) continue;
    const auto line_start = file->line_starts[line.line - 1];
    const auto prefix = contents.substr(line_start, symbol.declaration.begin.offset - line_start);
    if (prefix.find("auto ") == std::string_view::npos) continue;
    const auto position = analysis.sources.lsp_position(symbol.declaration.end);
    if (!first) out << ','; first = false;
    out << "{\"position\":{\"line\":" << position.line << ",\"character\":" << position.character
        << "},\"label\":\": " << json_escape(symbol.type_name) << "\",\"kind\":1}";
  }
  out << ']'; return out.str();
}

std::string lsp_semantic_tokens(const raz::compiler::FrontendAnalysis& analysis) {
  struct SemanticToken { std::uint32_t line, character, length; int type; };
  std::vector<SemanticToken> tokens;
  for (const auto& token : analysis.tokens) {
    if (token.kind >= TokenKind::kw_as && token.kind <= TokenKind::kw_while) {
      const auto start = analysis.sources.lsp_position(token.range.begin); const auto end = analysis.sources.lsp_position(token.range.end);
      tokens.push_back({start.line, start.character, end.character - start.character, 0});
    }
  }

  for (const auto& occurrence : analysis.occurrences) {
    if (!occurrence.symbol.has_value() || *occurrence.symbol >= analysis.symbols.size()) continue;
    const auto start = analysis.sources.lsp_position(occurrence.range.begin); const auto end = analysis.sources.lsp_position(occurrence.range.end);
    tokens.push_back({start.line, start.character, end.character - start.character,
                      lsp_semantic_token_type(analysis.symbols[*occurrence.symbol].kind)});
  }

  std::sort(tokens.begin(), tokens.end(), [](const auto& a, const auto& b) { return a.line != b.line ? a.line < b.line : a.character < b.character; });
  std::ostringstream data; data << '['; bool first = true; std::uint32_t previous_line = 0, previous_character = 0;
  for (const auto& token : tokens) {
    const auto delta_line = token.line - previous_line;
    const auto delta_character = delta_line == 0 ? token.character - previous_character : token.character;
    if (!first) data << ','; first = false;
    data << delta_line << ',' << delta_character << ',' << token.length << ',' << token.type << ",0";
    previous_line = token.line; previous_character = token.character;
  }
  data << ']'; return "{\"data\":" + data.str() + "}";
}

std::string lsp_semantic_workspace_symbols(const std::unordered_map<std::string, raz::compiler::FrontendAnalysis>& analyses,
                                           std::string_view query) {
  std::ostringstream out; out << '['; bool first = true;
  for (const auto& [uri, analysis] : analyses) for (const auto& symbol : analysis.symbols) {
    if (!lsp_global_symbol(symbol.kind) || (!query.empty() && symbol.name.find(query) == std::string::npos)) continue;
    if (!first) out << ','; first = false;
    out << "{\"name\":\"" << json_escape(symbol.name) << "\",\"kind\":" << lsp_symbol_kind(symbol.kind)
        << ",\"location\":{\"uri\":\"" << json_escape(uri) << "\",\"range\":" << lsp_range_json(analysis.sources, symbol.declaration) << "}}";
  }
  out << ']'; return out.str();
}

struct LspSymbol final {
  std::string name;
  std::string kind;
  std::size_t line = 0;
  std::size_t character = 0;
};

std::vector<LspSymbol> lsp_symbols(std::string_view text) {
  std::vector<LspSymbol> symbols;
  std::istringstream input{std::string(text)};
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    const auto trimmed = trim_copy(line);
    const std::pair<std::string_view, std::string_view> prefixes[] = {
      {"async fn ", "function"}, {"unsafe fn ", "function"}, {"fn ", "function"},
      {"struct ", "struct"}, {"enum ", "enum"}, {"trait ", "interface"},
      {"const ", "constant"}, {"static ", "variable"}
    };
    for (const auto& [prefix, kind] : prefixes) {
      auto candidate = std::string_view(trimmed);
      if (candidate.starts_with("public ")) candidate.remove_prefix(7);
      if (!candidate.starts_with(prefix)) continue;
      candidate.remove_prefix(prefix.size());
      const auto end = candidate.find_first_of("(<:{= \t");
      const auto name = std::string(candidate.substr(0, end));
      if (!name.empty()) {
        const auto character = line.find(name);
        symbols.push_back({name, std::string(kind), line_number, character == std::string::npos ? 0 : character});
      }
      break;
    }
    ++line_number;
  }
  return symbols;
}
