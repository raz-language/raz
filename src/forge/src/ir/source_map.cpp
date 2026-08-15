// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/source_map.hpp"
#include <sstream>
#include <string_view>

namespace forge::ir {
namespace {
void json_string(std::ostringstream& out, std::string_view value) {
    out << '"';
    for (const char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch; break;
        }
    }
    out << '"';
}

void attributes(std::ostringstream& out, const std::vector<Attribute>& values) {
    out << '{';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        json_string(out, values[i].name);
        out << ':';
        json_string(out, values[i].value);
    }
    out << '}';
}
}

std::string build_source_map_json(const Module& module) {
    std::ostringstream out;
    out << "{\"version\":1,\"module\":";
    json_string(out, module.name());
    out << ",\"metadata\":";
    attributes(out, module.metadata());
    out << ",\"operations\":[";
    bool first = true;
    for (const auto& function : module.functions()) {
        for (const auto& block : function.blocks) {
            for (std::size_t index = 0; index < block.operations.size(); ++index) {
                const auto& operation = block.operations[index];
                if (!first) out << ',';
                first = false;
                out << "{\"function\":"; json_string(out, function.name);
                out << ",\"block\":"; json_string(out, block.name);
                out << ",\"index\":" << index << ",\"opcode\":"; json_string(out, operation.opcode);
                out << ",\"result\":"; json_string(out, operation.result);
                out << ",\"file\":"; json_string(out, operation.source_file);
                out << ",\"line\":" << operation.source_line;
                out << ",\"column\":" << operation.source_column;
                out << ",\"endLine\":" << operation.source_end_line;
                out << ",\"endColumn\":" << operation.source_end_column;
                out << ",\"attributes\":"; attributes(out, operation.attributes);
                out << '}';
            }
        }
    }
    out << "]}";
    return out.str();
}
}
