// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

int run_lsp() {
  std::string line;
  bool shutdown = false;
  std::unordered_map<std::string, std::string> documents;
  std::unordered_map<std::string, raz::compiler::FrontendAnalysis> analyses;
  std::optional<raz::compiler::WorkspaceGraph> workspace_graph;
  std::set<std::string> workspace_dirty;
  while (std::getline(std::cin, line)) {
    std::size_t length = 0;
    if (line.rfind("Content-Length:", 0) == 0) length = static_cast<std::size_t>(std::stoul(trim_copy(line.substr(15))));
    while (std::getline(std::cin, line) && line != "\r" && !line.empty()) {}
    if (length == 0) continue;
    std::string body(length, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(length));
    const auto id_pos = body.find("\"id\"");
    std::string id = "null";
    if (id_pos != std::string::npos) {
      const auto colon = body.find(':', id_pos);
      const auto end = body.find_first_of(",}", colon + 1);
      id = trim_copy(body.substr(colon + 1, end - colon - 1));
    }
    const auto method = json_string_field(body, "method").value_or("");
    if (method == "initialize") {
      const auto root_uri = json_string_field(body, "rootUri").value_or("");
      if (!root_uri.empty()) {
        raz::compiler::ProjectGraph project;
        raz::compiler::ProjectError project_error;
        if (raz::compiler::discover_project(lsp_virtual_path(root_uri), project, project_error)) {
          workspace_graph = raz::compiler::build_workspace_graph(project);
        }
      }
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":{\"serverInfo\":{\"name\":\"raz-lsp\",\"version\":\"1.0.0\"},\"capabilities\":{\"textDocumentSync\":{\"openClose\":true,\"change\":1},\"documentFormattingProvider\":true,\"hoverProvider\":true,\"completionProvider\":{\"triggerCharacters\":[\".\",\":\"]},\"definitionProvider\":true,\"referencesProvider\":true,\"renameProvider\":true,\"workspaceSymbolProvider\":true,\"documentSymbolProvider\":true,\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"keyword\",\"function\",\"type\",\"variable\",\"parameter\",\"property\",\"enumMember\",\"namespace\"],\"tokenModifiers\":[]},\"full\":true},\"codeActionProvider\":{\"codeActionKinds\":[\"quickfix\",\"source.fixAll.raz\"]},\"inlayHintProvider\":true,\"foldingRangeProvider\":true,\"documentHighlightProvider\":true,\"selectionRangeProvider\":true}}}");
    } else if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto text = json_string_field(body, "text").value_or("");
      documents[uri] = text;
      if (workspace_graph.has_value()) {
        const auto dirty = workspace_graph->dirty_from_path(lsp_virtual_path(uri));
        workspace_dirty.insert(dirty.begin(), dirty.end());
      }
      auto analysis = lsp_analysis(uri, text);
      lsp_send(analysis.diagnostics.lsp_json(uri, analysis.sources));
      analyses.insert_or_assign(uri, std::move(analysis));
    } else if (method == "textDocument/didClose") {
      const auto uri = json_string_field(body, "uri").value_or("");
      documents.erase(uri);
      analyses.erase(uri);
      lsp_send("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" + json_escape(uri) + "\",\"diagnostics\":[]}}");
    } else if (method == "textDocument/formatting") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = documents.find(uri);
      const auto formatted = found == documents.end() ? std::string{} : format_raz_source(found->second);
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":2147483647,\"character\":0}},\"newText\":\"" + json_escape(formatted) + "\"}]}");
    } else if (method == "textDocument/hover") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = analyses.find(uri);
      std::string result = "null";
      if (found != analyses.end()) {
        const auto text = documents.find(uri);
        const auto position = lsp_request_position(body);
        const auto offset = text == documents.end() ? 0U : lsp_byte_offset(text->second, position);
        result = lsp_semantic_hover(found->second, offset);
      }
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}");
    } else if (method == "textDocument/completion") {
      const auto uri = json_string_field(body, "uri").value_or("");
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + lsp_semantic_completion(analyses, uri) + "}");
    } else if (method == "textDocument/signatureHelp") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = analyses.find(uri); const auto text = documents.find(uri);
      const auto offset = text == documents.end() ? 0U : lsp_byte_offset(text->second, lsp_request_position(body));
      const auto result = found == analyses.end() ? std::string("null") : lsp_semantic_signature_help(found->second, offset);
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}");
    } else if (method == "textDocument/semanticTokens/full") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = analyses.find(uri);
      const auto result = found == analyses.end() ? std::string("{\"data\":[]}") : lsp_semantic_tokens(found->second);
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}");
    } else if (method == "textDocument/documentSymbol") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = analyses.find(uri);
      const auto result = found == analyses.end() ? std::string("[]") : lsp_semantic_document_symbols(found->second);
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}");
    } else if (method == "textDocument/definition") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto text = documents.find(uri);
      const auto offset = text == documents.end() ? 0U : lsp_byte_offset(text->second, lsp_request_position(body));
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + lsp_semantic_definition(analyses, uri, offset) + "}");
    } else if (method == "workspace/symbol") {
      const auto query = json_string_field(body, "query").value_or("");
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + lsp_semantic_workspace_symbols(analyses, query) + "}");
    } else if (method == "textDocument/references") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto text = documents.find(uri);
      const auto offset = text == documents.end() ? 0U : lsp_byte_offset(text->second, lsp_request_position(body));
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + lsp_semantic_references(analyses, uri, offset) + "}");
    } else if (method == "textDocument/rename") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto replacement = json_string_field(body, "newName").value_or("");
      const auto text = documents.find(uri);
      const auto offset = text == documents.end() ? 0U : lsp_byte_offset(text->second, lsp_request_position(body));
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + lsp_semantic_rename(analyses, uri, offset, replacement) + "}");
    } else if (method == "textDocument/codeAction") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = documents.find(uri);
      std::string result = "[]";
      if (found != documents.end()) {
        const auto analyzed = analyses.find(uri);
        const auto quick = analyzed == analyses.end() ? std::string("[]") : analyzed->second.diagnostics.lsp_code_actions_json(uri, analyzed->second.sources);
        const auto formatted = format_raz_source(found->second);
        if (formatted == found->second) {
          result = quick;
        } else {
          const std::string format_action = "{\"title\":\"Format Raz document\",\"kind\":\"source.fixAll.raz\",\"edit\":{\"changes\":{\"" +
              json_escape(uri) + "\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":2147483647,\"character\":0}},\"newText\":\"" +
              json_escape(formatted) + "\"}]}}}";
          if (quick == "[]") result = "[" + format_action + "]";
          else result = quick.substr(0, quick.size() - 1) + "," + format_action + "]";
        }
      }
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}");
    } else if (method == "textDocument/inlayHint") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = analyses.find(uri);
      const auto result = found == analyses.end() ? std::string("[]") : lsp_semantic_inlay_hints(found->second);
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}");
    } else if (method == "textDocument/foldingRange") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto found = documents.find(uri);
      std::ostringstream ranges; ranges << '['; bool first = true;
      if (found != documents.end()) {
        std::vector<std::size_t> stack; std::istringstream input(found->second); std::string source_line; std::size_t line_no = 0;
        while (std::getline(input, source_line)) {
          for (const char c : source_line) {
            if (c == '{') stack.push_back(line_no);
            else if (c == '}' && !stack.empty()) { const auto start = stack.back(); stack.pop_back(); if (line_no > start) { if (!first) ranges << ','; first = false; ranges << "{\"startLine\":" << start << ",\"endLine\":" << line_no << ",\"kind\":\"region\"}"; } }
          }
          ++line_no;
        }
      }
      ranges << ']'; lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + ranges.str() + "}");
    } else if (method == "textDocument/documentHighlight") {
      const auto uri = json_string_field(body, "uri").value_or("");
      const auto current = analyses.find(uri); const auto text = documents.find(uri);
      const auto offset = text == documents.end() ? 0U : lsp_byte_offset(text->second, lsp_request_position(body));
      std::string refs = lsp_semantic_references(analyses, uri, offset);
      std::ostringstream highlights; highlights << '['; bool first = true;
      if (current != analyses.end()) {
        const auto* target = lsp_occurrence_at(current->second, offset);
        if (target != nullptr) {
          bool global = true; std::optional<std::size_t> local_symbol;
          if (target->symbol.has_value() && *target->symbol < current->second.symbols.size()) {
            global = lsp_global_symbol(current->second.symbols[*target->symbol].kind); if (!global) local_symbol = target->symbol;
          }
          for (const auto& occurrence : current->second.occurrences) {
            if (occurrence.name != target->name || (!global && occurrence.symbol != local_symbol)) continue;
            if (!first) highlights << ','; first = false;
            highlights << "{\"range\":" << lsp_range_json(current->second.sources, occurrence.range) << ",\"kind\":1}";
          }
        }
      }
      highlights << ']'; (void)refs;
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + highlights.str() + "}");
    } else if (method == "textDocument/selectionRange") {
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":2147483647,\"character\":0}}}]}");
    } else if (method == "shutdown") {
      shutdown = true; lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
    } else if (method == "exit") {
      return shutdown ? 0 : 1;
    } else if (id != "null") {
      lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":-32601,\"message\":\"method not implemented\"}}");
    }
  }
  return 0;
}
