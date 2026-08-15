// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/incremental.hpp"
#include "forge/ir/printer.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>

namespace forge::ir {
namespace {

constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

constexpr std::uint32_t rotr(std::uint32_t value, unsigned amount) noexcept {
    return (value >> amount) | (value << (32U - amount));
}

std::string sha256(std::string_view input) {
    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    bytes.push_back(0x80U);
    while ((bytes.size() % 64U) != 56U) bytes.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>((bit_length >> static_cast<unsigned>(shift)) & 0xffU));

    std::array<std::uint32_t, 8> hash = {
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U
    };
    std::array<std::uint32_t, 64> words{};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64U) {
        for (std::size_t i = 0; i < 16U; ++i) {
            const auto base = offset + i * 4U;
            words[i] = (static_cast<std::uint32_t>(bytes[base]) << 24U) |
                       (static_cast<std::uint32_t>(bytes[base + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(bytes[base + 2U]) << 8U) |
                       static_cast<std::uint32_t>(bytes[base + 3U]);
        }
        for (std::size_t i = 16U; i < 64U; ++i) {
            const auto s0 = rotr(words[i - 15U], 7U) ^ rotr(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
            const auto s1 = rotr(words[i - 2U], 17U) ^ rotr(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
            words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
        }
        auto a = hash[0]; auto b = hash[1]; auto c = hash[2]; auto d = hash[3];
        auto e = hash[4]; auto f = hash[5]; auto g = hash[6]; auto h = hash[7];
        for (std::size_t i = 0; i < 64U; ++i) {
            const auto s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
            const auto choice = (e & f) ^ ((~e) & g);
            const auto temp1 = h + s1 + choice + k[i] + words[i];
            const auto s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto value : hash) out << std::setw(8) << value;
    return out.str();
}

void append_field(std::string& out, std::string_view name, std::string_view value) {
    out.append(name);
    out.push_back(':');
    out.append(std::to_string(value.size()));
    out.push_back(':');
    out.append(value);
    out.push_back('\n');
}

void append_attributes(std::string& out, const std::vector<Attribute>& attributes) {
    std::vector<std::pair<std::string, std::string>> sorted;
    sorted.reserve(attributes.size());
    for (const auto& attribute : attributes) sorted.emplace_back(attribute.name, attribute.value);
    std::sort(sorted.begin(), sorted.end());
    for (const auto& [name, value] : sorted) {
        append_field(out, "attribute-name", name);
        append_field(out, "attribute-value", value);
    }
}

std::string semantic_text(const Function& function) {
    Module temporary("incremental-function");
    temporary.functions().push_back(function);
    for (auto& block : temporary.functions().front().blocks)
        for (auto& operation : block.operations) {
            operation.source_file.clear();
            operation.source_line = 0;
            operation.source_column = 0;
            operation.source_end_line = 0;
            operation.source_end_column = 0;
            operation.attributes.clear();
        }
    return print_module(temporary);
}

std::string frontend_text(const Function& function) {
    std::string out = semantic_text(function);
    for (const auto& block : function.blocks) {
        append_field(out, "block", block.name);
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            const auto& operation = block.operations[index];
            append_field(out, "operation-index", std::to_string(index));
            append_field(out, "source-file", operation.source_file);
            append_field(out, "source-line", std::to_string(operation.source_line));
            append_field(out, "source-column", std::to_string(operation.source_column));
            append_field(out, "source-end-line", std::to_string(operation.source_end_line));
            append_field(out, "source-end-column", std::to_string(operation.source_end_column));
            append_attributes(out, operation.attributes);
        }
    }
    return out;
}

std::string module_semantic_text(const Module& module) {
    Module copy = module;
    copy.metadata().clear();
    for (auto& function : copy.functions())
        for (auto& block : function.blocks)
            for (auto& operation : block.operations) {
                operation.source_file.clear();
                operation.source_line = 0;
                operation.source_column = 0;
                operation.source_end_line = 0;
                operation.source_end_column = 0;
                operation.attributes.clear();
            }
    return print_module(copy);
}

std::string module_frontend_text(const Module& module) {
    std::string out = module_semantic_text(module);
    append_attributes(out, module.metadata());
    std::vector<const Function*> functions;
    functions.reserve(module.functions().size());
    for (const auto& function : module.functions()) functions.push_back(&function);
    std::sort(functions.begin(), functions.end(), [](const Function* lhs, const Function* rhs) { return lhs->name < rhs->name; });
    for (const auto* function : functions) append_field(out, "function-frontend", frontend_text(*function));
    return out;
}

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

} // namespace

IncrementalSnapshot build_incremental_snapshot(const Module& module) {
    IncrementalSnapshot snapshot;
    snapshot.semantic_fingerprint = sha256(module_semantic_text(module));
    snapshot.frontend_fingerprint = sha256(module_frontend_text(module));
    snapshot.functions.reserve(module.functions().size());
    for (const auto& function : module.functions()) {
        snapshot.functions.push_back({function.name, sha256(semantic_text(function)), sha256(frontend_text(function))});
    }

    std::sort(snapshot.functions.begin(), snapshot.functions.end(),
              [](const FunctionFingerprint& lhs, const FunctionFingerprint& rhs) { return lhs.name < rhs.name; });
    return snapshot;
}

std::vector<FunctionChange> compare_incremental_snapshots(const IncrementalSnapshot& previous,
                                                          const IncrementalSnapshot& current,
                                                          bool include_unchanged) {
    std::map<std::string, FunctionFingerprint> before;
    std::map<std::string, FunctionFingerprint> after;
    for (const auto& function : previous.functions) before.emplace(function.name, function);
    for (const auto& function : current.functions) after.emplace(function.name, function);
    std::vector<FunctionChange> changes;
    std::map<std::string, bool> names;
    for (const auto& [name, unused] : before) { (void)unused; names.emplace(name, true); }
    for (const auto& [name, unused] : after) { (void)unused; names.emplace(name, true); }
    for (const auto& [name, unused] : names) {
        (void)unused;
        const auto old_it = before.find(name);
        const auto new_it = after.find(name);
        FunctionChange change;
        change.name = name;
        if (old_it == before.end()) {
            change.kind = FunctionChangeKind::added;
            change.current_semantic_fingerprint = new_it->second.semantic_fingerprint;
            change.current_frontend_fingerprint = new_it->second.frontend_fingerprint;
        } else if (new_it == after.end()) {
            change.kind = FunctionChangeKind::removed;
            change.previous_semantic_fingerprint = old_it->second.semantic_fingerprint;
            change.previous_frontend_fingerprint = old_it->second.frontend_fingerprint;
        } else {
            change.previous_semantic_fingerprint = old_it->second.semantic_fingerprint;
            change.current_semantic_fingerprint = new_it->second.semantic_fingerprint;
            change.previous_frontend_fingerprint = old_it->second.frontend_fingerprint;
            change.current_frontend_fingerprint = new_it->second.frontend_fingerprint;
            if (change.previous_semantic_fingerprint != change.current_semantic_fingerprint)
                change.kind = FunctionChangeKind::modified;
            else if (change.previous_frontend_fingerprint != change.current_frontend_fingerprint)
                change.kind = FunctionChangeKind::frontend_only;
            else
                change.kind = FunctionChangeKind::unchanged;
        }
        if (include_unchanged || change.kind != FunctionChangeKind::unchanged) changes.push_back(std::move(change));
    }
    return changes;
}

std::string build_incremental_manifest_json(const IncrementalSnapshot& snapshot) {
    std::ostringstream out;
    out << "{\"version\":1,\"semanticFingerprint\":";
    json_string(out, snapshot.semantic_fingerprint);
    out << ",\"frontendFingerprint\":";
    json_string(out, snapshot.frontend_fingerprint);
    out << ",\"functions\":[";
    for (std::size_t index = 0; index < snapshot.functions.size(); ++index) {
        if (index != 0) out << ',';
        const auto& function = snapshot.functions[index];
        out << "{\"name\":"; json_string(out, function.name);
        out << ",\"semanticFingerprint\":"; json_string(out, function.semantic_fingerprint);
        out << ",\"frontendFingerprint\":"; json_string(out, function.frontend_fingerprint);
        out << '}';
    }
    out << "]}";
    return out.str();
}

std::string build_cache_key(const Module& module, std::string_view frontend_id, std::string_view configuration) {
    const auto snapshot = build_incremental_snapshot(module);
    std::string text;
    append_field(text, "schema", "forge-incremental-v1");
    append_field(text, "frontend", frontend_id);
    append_field(text, "configuration", configuration);
    append_field(text, "semantic", snapshot.semantic_fingerprint);
    append_field(text, "frontend-state", snapshot.frontend_fingerprint);
    return sha256(text);
}

std::string_view function_change_kind_name(FunctionChangeKind kind) noexcept {
    switch (kind) {
    case FunctionChangeKind::added: return "added";
    case FunctionChangeKind::removed: return "removed";
    case FunctionChangeKind::modified: return "modified";
    case FunctionChangeKind::frontend_only: return "frontend-only";
    case FunctionChangeKind::unchanged: return "unchanged";
    }
    return "unknown";
}

} // namespace forge::ir
