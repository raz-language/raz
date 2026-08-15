// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/diagnostics/diagnostic_engine.hpp"

#include "compiler/source/source_manager.hpp"
#include "common/terminal.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace raz::compiler {
namespace {

const char* severity_name(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::note: return "note";
    case DiagnosticSeverity::warning: return "warning";
    case DiagnosticSeverity::error: return "error";
    case DiagnosticSeverity::fatal: return "fatal error";
  }
  return "error";
}

std::string_view diagnostic_category(std::string_view code) {
  if (code.size() < 2 || code[0] != 'D') return "other";
  switch (code[1]) {
    case '0': return "lexer";
    case '1': return "parser";
    case '2': return "semantic";
    case '3': return "lowering";
    case '4': return "backend";
    default: return "other";
  }
}

int lsp_severity(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::error:
    case DiagnosticSeverity::fatal: return 1;
    case DiagnosticSeverity::warning: return 2;
    case DiagnosticSeverity::note: return 3;
  }
  return 1;
}

std::string json_escape(std::string_view value) {
  std::ostringstream out;
  for (const unsigned char c : value) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20U) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  return out.str();
}

SourceRange primary_range(const Diagnostic& diagnostic) {
  for (const auto& label : diagnostic.labels) {
    if (label.primary && label.range.valid()) return label.range;
  }
  for (const auto& label : diagnostic.labels) {
    if (label.range.valid()) return label.range;
  }
  return {};
}

bool diagnostic_pattern_matches(std::string_view pattern, std::string_view code) {
  if (pattern == "warnings" || pattern == "all") return true;
  if (pattern == "lexer") return code.size() >= 2 && code[0] == 'D' && code[1] == '0';
  if (pattern == "parser") return code.size() >= 2 && code[0] == 'D' && code[1] == '1';
  if (pattern == "semantic") return code.size() >= 2 && code[0] == 'D' && code[1] == '2';
  if (pattern == "lowering") return code.size() >= 2 && code[0] == 'D' && code[1] == '3';
  if (pattern == "backend") return code.size() >= 2 && code[0] == 'D' && code[1] == '4';
  return pattern == code;
}

DiagnosticLevel policy_level(const DiagnosticPolicy& policy, std::string_view code) {
  DiagnosticLevel level = policy.deny_warnings ? DiagnosticLevel::deny : DiagnosticLevel::warn;
  for (const auto& override : policy.overrides) {
    if (diagnostic_pattern_matches(override.pattern, code)) level = override.level;
  }
  return level;
}

std::vector<std::string> effective_help(const Diagnostic& diagnostic) {
  if (!diagnostic.help.empty()) return diagnostic.help;
  if (diagnostic.code == "D2008")
    return {"check the spelling, import the symbol, or qualify it with its namespace"};
  if (diagnostic.code == "D2053" || diagnostic.code == "D2054")
    return {"use the value before moving it, clone it when appropriate, or restructure ownership so the move happens later"};
  if (diagnostic.code == "D2055" || diagnostic.code == "D2056" || diagnostic.code == "D2057")
    return {"shorten the active borrow or avoid overlapping mutable and shared access"};
  if (diagnostic.code == "D3001")
    return {"return a value on every control-flow path or change the function return type"};
  if (diagnostic.code == "D1001")
    return {"check the token immediately before the highlighted location; it often determines what the parser expected here"};
  if (diagnostic.code == "D2210")
    return {"rename one declaration or place it in a different namespace"};
  if (diagnostic.code == "D2211")
    return {"use an explicit import alias or a fully qualified path to disambiguate this symbol"};
  return {};
}

void json_position(std::ostream& out, const SourceManager& sources,
                   SourceLocation location) {
  const auto lsp = sources.display_lsp_position(location);
  out << "{\"line\":" << lsp.line << ",\"character\":" << lsp.character
      << ",\"byte_offset\":" << sources.display_byte_offset(location) << '}';
}

void json_range(std::ostream& out, const SourceManager& sources,
                SourceRange range) {
  out << "{\"start\":";
  json_position(out, sources, range.begin);
  out << ",\"end\":";
  json_position(out, sources, range.end);
  out << '}';
}

void render_human(const std::vector<Diagnostic>& diagnostics,
                  std::ostream& stream, const SourceManager& sources) {
  const bool color = terminal::color_enabled(stream);
  const auto severity_color = [](DiagnosticSeverity severity) -> std::string_view {
    switch (severity) {
      case DiagnosticSeverity::note: return terminal::cyan;
      case DiagnosticSeverity::warning: return terminal::yellow;
      case DiagnosticSeverity::error:
      case DiagnosticSeverity::fatal: return terminal::red;
    }
    return terminal::red;
  };

  for (std::size_t diagnostic_index = 0; diagnostic_index < diagnostics.size(); ++diagnostic_index) {
    const auto& diagnostic = diagnostics[diagnostic_index];
    const auto primary = primary_range(diagnostic);
    const auto physical_location = sources.line_column(primary.begin);
    const auto location = sources.display_line_column(primary.begin);
    const auto sev_color = severity_color(diagnostic.severity);

    if (color) stream << terminal::bold << sev_color;
    stream << severity_name(diagnostic.severity);
    if (!diagnostic.code.empty()) stream << '[' << diagnostic.code << ']';
    stream << ": ";
    if (color) stream << terminal::reset << terminal::bold;
    stream << diagnostic.message;
    if (color) stream << terminal::reset;
    stream << '\n';

    const auto* file = sources.get(primary.begin.file);
    if (file != nullptr && primary.valid()) {
      if (color) stream << terminal::blue << terminal::bold;
      stream << "  --> ";
      if (color) stream << terminal::reset;
      stream << sources.display_name(primary.begin.file) << ':' << location.line << ':' << location.column << '\n';

      const auto line_start = file->line_starts[physical_location.line - 1];
      const auto line_end_pos = file->text.find('\n', line_start);
      const auto line_end = line_end_pos == std::string::npos ? file->text.size() : line_end_pos;
      const auto line = std::string_view(file->text).substr(line_start, line_end - line_start);
      const auto line_digits = std::to_string(location.line).size();
      if (color) stream << terminal::blue;
      stream << std::string(line_digits + 1, ' ') << " |" << '\n';
      stream << ' ' << location.line << " | ";
      if (color) stream << terminal::reset;
      stream << line << '\n';
      if (color) stream << terminal::blue;
      stream << std::string(line_digits + 1, ' ') << " | ";
      if (color) stream << sev_color << terminal::bold;
      for (std::uint32_t column = 1; column < location.column; ++column) stream << ' ';
      const auto line_remaining = location.column <= line.size() + 1 ? line.size() + 1 - location.column : std::size_t{1};
      const auto marker_length = std::max<std::size_t>(1, std::min<std::size_t>(primary.size(), line_remaining));
      stream << '^';
      for (std::size_t i = 1; i < marker_length; ++i) stream << '~';
      for (const auto& label : diagnostic.labels) {
        if (label.primary && !label.message.empty()) stream << ' ' << label.message;
      }
      if (color) stream << terminal::reset;
      stream << '\n';

      for (const auto& label : diagnostic.labels) {
        if (label.primary || !label.range.valid() || label.message.empty()) continue;
        const auto secondary = sources.display_line_column(label.range.begin);
        if (color) stream << terminal::cyan;
        stream << std::string(line_digits + 1, ' ') << " = note: ";
        if (color) stream << terminal::reset;
        stream << label.message << " (" << sources.display_name(label.range.begin.file) << ':'
               << secondary.line << ':' << secondary.column << ")\n";
      }
    }

    for (const auto& note : diagnostic.notes) {
      if (color) stream << terminal::cyan;
      stream << "  = note: ";
      if (color) stream << terminal::reset;
      stream << note << '\n';
    }
    for (const auto& help : effective_help(diagnostic)) {
      if (color) stream << terminal::cyan << terminal::bold;
      stream << "  = help: ";
      if (color) stream << terminal::reset;
      stream << help << '\n';
    }
    for (const auto& fix : diagnostic.fixes) {
      if (color) stream << terminal::green << terminal::bold;
      stream << "  = fix: ";
      if (color) stream << terminal::reset;
      if (!fix.message.empty()) stream << fix.message << ": ";
      stream << "replace with `" << fix.replacement << "`\n";
    }
    if (diagnostic_index + 1 != diagnostics.size()) stream << '\n';
  }
}

}  // namespace

void DiagnosticEngine::set_policy(DiagnosticPolicy policy) { policy_ = std::move(policy); }

void DiagnosticEngine::report(Diagnostic diagnostic) {
  if (diagnostic.severity == DiagnosticSeverity::warning) {
    const auto level = policy_level(policy_, diagnostic.code);
    if (level == DiagnosticLevel::allow) return;
    if (level == DiagnosticLevel::deny) diagnostic.severity = DiagnosticSeverity::error;
  }

  if (diagnostic.severity == DiagnosticSeverity::error ||
      diagnostic.severity == DiagnosticSeverity::fatal) {
    ++error_count_;
  }
  diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticEngine::error(std::string code, SourceRange range,
                             std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = DiagnosticSeverity::error;
  diagnostic.code = std::move(code);
  diagnostic.message = std::move(message);
  diagnostic.labels.push_back(DiagnosticLabel{range, {}, true});
  report(std::move(diagnostic));
}

void DiagnosticEngine::warning(std::string code, SourceRange range,
                               std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = DiagnosticSeverity::warning;
  diagnostic.code = std::move(code);
  diagnostic.message = std::move(message);
  diagnostic.labels.push_back(DiagnosticLabel{range, {}, true});
  report(std::move(diagnostic));
}

bool DiagnosticEngine::has_errors() const noexcept { return error_count_ != 0; }
std::size_t DiagnosticEngine::error_count() const noexcept { return error_count_; }
const std::vector<Diagnostic>& DiagnosticEngine::diagnostics() const noexcept { return diagnostics_; }

void DiagnosticEngine::render(std::ostream& stream, const SourceManager& sources,
                              DiagnosticFormat format) const {
  if (format == DiagnosticFormat::json) {
    stream << json(sources) << '\n';
    return;
  }

  if (format == DiagnosticFormat::short_) {
    for (const auto& diagnostic : diagnostics_) {
      const auto range = primary_range(diagnostic);
      const auto location = sources.display_line_column(range.begin);
      stream << sources.display_name(range.begin.file) << ':' << location.line << ':' << location.column << ": "
             << severity_name(diagnostic.severity);
      if (!diagnostic.code.empty()) stream << '[' << diagnostic.code << ']';
      stream << ": " << diagnostic.message << '\n';
    }
    return;
  }

  render_human(diagnostics_, stream, sources);
}

std::string DiagnosticEngine::json(const SourceManager& sources) const {
  std::ostringstream out;
  out << "{\"schema\":\"raz-diagnostics-v1\",\"error_count\":" << error_count_ << ",\"diagnostics\":[";
  for (std::size_t i = 0; i < diagnostics_.size(); ++i) {
    if (i != 0) out << ',';
    const auto& diagnostic = diagnostics_[i];
    const auto primary = primary_range(diagnostic);
    out << "{\"severity\":\"" << severity_name(diagnostic.severity) << "\",\"code\":\""
        << json_escape(diagnostic.code) << "\",\"category\":\"" << diagnostic_category(diagnostic.code)
        << "\",\"message\":\"" << json_escape(diagnostic.message)
        << "\",\"file\":\"" << json_escape(sources.display_name(primary.begin.file)) << "\",\"range\":";
    json_range(out, sources, primary);
    out << ",\"labels\":[";
    for (std::size_t label_index = 0; label_index < diagnostic.labels.size(); ++label_index) {
      if (label_index != 0) out << ',';
      const auto& label = diagnostic.labels[label_index];
      out << "{\"primary\":" << (label.primary ? "true" : "false") << ",\"message\":\""
          << json_escape(label.message) << "\",\"range\":";
      json_range(out, sources, label.range);
      out << '}';
    }
    out << "],\"notes\":[";
    for (std::size_t note_index = 0; note_index < diagnostic.notes.size(); ++note_index) {
      if (note_index != 0) out << ',';
      out << '"' << json_escape(diagnostic.notes[note_index]) << '"';
    }
    out << "],\"help\":[";
    const auto help = effective_help(diagnostic);
    for (std::size_t help_index = 0; help_index < help.size(); ++help_index) {
      if (help_index != 0) out << ',';
      out << '"' << json_escape(help[help_index]) << '"';
    }
    out << "],\"fixes\":[";
    for (std::size_t fix_index = 0; fix_index < diagnostic.fixes.size(); ++fix_index) {
      if (fix_index != 0) out << ',';
      const auto& fix = diagnostic.fixes[fix_index];
      out << "{\"message\":\"" << json_escape(fix.message) << "\",\"replacement\":\""
          << json_escape(fix.replacement) << "\",\"range\":";
      json_range(out, sources, fix.range);
      out << '}';
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

std::string DiagnosticEngine::lsp_json(std::string_view uri, const SourceManager& sources) const {
  std::ostringstream out;
  out << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\""
      << json_escape(uri) << "\",\"diagnostics\":[";
  for (std::size_t i = 0; i < diagnostics_.size(); ++i) {
    if (i != 0) out << ',';
    const auto& diagnostic = diagnostics_[i];
    const auto range = primary_range(diagnostic);
    const auto start = sources.lsp_position(range.begin);
    const auto end = sources.lsp_position(range.end);
    out << "{\"range\":{\"start\":{\"line\":" << start.line << ",\"character\":" << start.character
        << "},\"end\":{\"line\":" << end.line << ",\"character\":" << end.character
        << "}},\"severity\":" << lsp_severity(diagnostic.severity) << ",\"source\":\"raz\",\"code\":\""
        << json_escape(diagnostic.code) << "\",\"message\":\"" << json_escape(diagnostic.message) << '"';
    if (diagnostic.code == "D2052") out << ",\"tags\":[1]";
    bool has_related = false;
    for (const auto& label : diagnostic.labels) {
      if (!label.primary && label.range.valid() && !label.message.empty()) { has_related = true; break; }
    }
    if (has_related) {
      out << ",\"relatedInformation\":[";
      bool first_related = true;
      for (const auto& label : diagnostic.labels) {
        if (label.primary || !label.range.valid() || label.message.empty()) continue;
        if (!first_related) out << ',';
        first_related = false;
        const auto related_start = sources.lsp_position(label.range.begin);
        const auto related_end = sources.lsp_position(label.range.end);
        out << "{\"location\":{\"uri\":\"" << json_escape(uri)
            << "\",\"range\":{\"start\":{\"line\":" << related_start.line
            << ",\"character\":" << related_start.character << "},\"end\":{\"line\":"
            << related_end.line << ",\"character\":" << related_end.character
            << "}}},\"message\":\"" << json_escape(label.message) << "\"}";
      }
      out << ']';
    }
    const auto help = effective_help(diagnostic);
    if (!help.empty() || !diagnostic.notes.empty()) {
      std::string detail;
      for (const auto& note : diagnostic.notes) {
        if (!detail.empty()) detail += "\n";
        detail += "note: " + note;
      }
      for (const auto& item : help) {
        if (!detail.empty()) detail += "\n";
        detail += "help: " + item;
      }
      out << ",\"data\":{\"category\":\"" << diagnostic_category(diagnostic.code)
          << "\",\"detail\":\"" << json_escape(detail) << "\",\"fixes\":[";
      for (std::size_t fix_index = 0; fix_index < diagnostic.fixes.size(); ++fix_index) {
        if (fix_index != 0) out << ',';
        const auto& fix = diagnostic.fixes[fix_index];
        const auto fix_start = sources.lsp_position(fix.range.begin);
        const auto fix_end = sources.lsp_position(fix.range.end);
        out << "{\"title\":\"" << json_escape(fix.message.empty() ? std::string("Apply Raz fix") : fix.message)
            << "\",\"replacement\":\"" << json_escape(fix.replacement) << "\",\"range\":{\"start\":{\"line\":"
            << fix_start.line << ",\"character\":" << fix_start.character << "},\"end\":{\"line\":"
            << fix_end.line << ",\"character\":" << fix_end.character << "}}}";
      }
      out << "]}";
    }
    out << '}';
  }
  out << "]}}";
  return out.str();
}

std::string DiagnosticEngine::lsp_code_actions_json(
    std::string_view uri, const SourceManager& sources) const {
  std::ostringstream out;
  out << '[';
  bool first = true;
  for (const auto& diagnostic : diagnostics_) {
    for (const auto& fix : diagnostic.fixes) {
      if (!fix.range.valid()) continue;
      if (!first) out << ',';
      first = false;
      const auto start = sources.lsp_position(fix.range.begin);
      const auto end = sources.lsp_position(fix.range.end);
      const auto title = fix.message.empty() ? std::string("Apply Raz fix") : fix.message;
      out << "{\"title\":\"" << json_escape(title) << "\",\"kind\":\"quickfix\",\"isPreferred\":true,"
          << "\"diagnostics\":[{\"code\":\"" << json_escape(diagnostic.code) << "\",\"message\":\""
          << json_escape(diagnostic.message) << "\",\"range\":{\"start\":{\"line\":" << start.line
          << ",\"character\":" << start.character << "},\"end\":{\"line\":" << end.line
          << ",\"character\":" << end.character << "}}}],\"edit\":{\"changes\":{\""
          << json_escape(uri) << "\":[{\"range\":{\"start\":{\"line\":" << start.line
          << ",\"character\":" << start.character << "},\"end\":{\"line\":" << end.line
          << ",\"character\":" << end.character << "}},\"newText\":\"" << json_escape(fix.replacement)
          << "\"}]}}}";
    }
  }
  out << ']';
  return out.str();
}

void DiagnosticEngine::clear() {
  diagnostics_.clear();
  error_count_ = 0;
}

}  // namespace raz::compiler
