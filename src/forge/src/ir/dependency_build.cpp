// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/dependency_build.hpp"
#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

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

std::size_t resolve_workers(std::size_t requested, std::size_t tasks) {
    if (tasks == 0) return 0;
    if (requested == 0) requested = std::max(1U, std::thread::hardware_concurrency());
    return std::max<std::size_t>(1, std::min(requested, tasks));
}

const FunctionDependencyNode* find_node(const FunctionDependencyGraph& graph, std::string_view name) {
    const auto it = std::lower_bound(graph.functions.begin(), graph.functions.end(), name,
        [](const FunctionDependencyNode& node, std::string_view value) { return node.name < value; });
    return it != graph.functions.end() && it->name == name ? &*it : nullptr;
}

std::string dependency_key(std::string_view original,
                           const FunctionDependencyNode& node,
                           std::string_view frontend_id,
                           std::string_view configuration,
                           ArtifactKind kind) {
    Module key_module("dependency-cache-key");
    key_module.metadata().push_back({"dependency.original", std::string(original)});
    key_module.metadata().push_back({"dependency.function", node.name});
    key_module.metadata().push_back({"dependency.frontend", std::string(frontend_id)});
    key_module.metadata().push_back({"dependency.configuration", std::string(configuration)});
    key_module.metadata().push_back({"dependency.kind", std::string(artifact_kind_name(kind))});
    for (const auto& callee : node.callees)
        key_module.metadata().push_back({"dependency.callee", callee});
    return build_cache_key(key_module, "forge-dependency-artifact-v1", "semantic");
}
}

FunctionDependencyGraph build_function_dependency_graph(const Module& module) {
    FunctionDependencyGraph graph;
    std::map<std::string, std::set<std::string>> callees;
    std::map<std::string, std::set<std::string>> callers;
    for (const auto& function : module.functions()) {
        callees[function.name];
        callers[function.name];
    }

    for (const auto& function : module.functions()) {
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations) {
                if (operation.opcode != "call" || operation.operands.empty()) continue;
                const auto& target = operation.operands.front();
                if (!target.starts_with('@')) continue;
                const auto callee = target.substr(1);
                if (!callees.contains(callee)) continue;
                callees[function.name].insert(callee);
                callers[callee].insert(function.name);
            }
        }
    }
    graph.functions.reserve(callees.size());
    for (const auto& [name, targets] : callees) {
        FunctionDependencyNode node;
        node.name = name;
        node.callees.assign(targets.begin(), targets.end());
        const auto& sources = callers[name];
        node.callers.assign(sources.begin(), sources.end());
        graph.functions.push_back(std::move(node));
    }
    return graph;
}

IncrementalBuildPlan propagate_dependency_invalidations(const IncrementalBuildPlan& plan,
                                                         const FunctionDependencyGraph& graph,
                                                         std::string_view frontend_id,
                                                         std::string_view configuration,
                                                         ArtifactKind kind) {
    IncrementalBuildPlan result = plan;
    std::map<std::string, std::size_t> indexes;
    for (std::size_t index = 0; index < result.functions.size(); ++index)
        indexes.emplace(result.functions[index].name, index);
    std::queue<std::string> pending;
    std::set<std::string> invalidated;
    for (const auto& decision : result.functions) {
        if (decision.action == BuildAction::rebuild || decision.action == BuildAction::remove) {
            pending.push(decision.name);
            invalidated.insert(decision.name);
        }
    }

    while (!pending.empty()) {
        const auto changed = pending.front();
        pending.pop();
        const auto* node = find_node(graph, changed);
        if (node == nullptr) continue;
        for (const auto& caller : node->callers) {
            if (!invalidated.insert(caller).second) continue;
            pending.push(caller);
            const auto found = indexes.find(caller);
            if (found == indexes.end()) continue;
            auto& decision = result.functions[found->second];
            if (decision.action != BuildAction::remove) decision.action = BuildAction::rebuild;
        }
    }

    for (auto& decision : result.functions) {
        if (decision.action != BuildAction::rebuild) continue;
        const auto* node = find_node(graph, decision.name);
        if (node == nullptr) continue;
        decision.semantic_cache_key = dependency_key(
            decision.semantic_cache_key, *node, frontend_id, configuration, kind);
    }
    return result;
}

DependencyBuildSchedule build_dependency_build_schedule(const IncrementalBuildPlan& plan,
                                                         const FunctionDependencyGraph& graph,
                                                         std::size_t requested_workers) {
    DependencyBuildSchedule schedule;
    schedule.requested_workers = requested_workers;
    std::map<std::string, FunctionBuildDecision> work;
    for (const auto& decision : plan.functions)
        if (decision.action != BuildAction::remove) work.emplace(decision.name, decision);
    schedule.worker_count = resolve_workers(requested_workers, work.size());

    std::map<std::string, std::size_t> unresolved;
    std::map<std::string, std::vector<std::string>> callers;
    for (const auto& [name, decision] : work) {
        static_cast<void>(decision);
        unresolved[name] = 0;
    }

    for (const auto& [name, decision] : work) {
        static_cast<void>(decision);
        const auto* node = find_node(graph, name);
        if (node == nullptr) continue;
        for (const auto& callee : node->callees) {
            if (!work.contains(callee) || callee == name) continue;
            ++unresolved[name];
            callers[callee].push_back(name);
        }
    }
    std::set<std::string> ready;
    for (const auto& [name, count] : unresolved) if (count == 0) ready.insert(name);
    std::set<std::string> emitted;
    while (!ready.empty()) {
        DependencyBuildLevel level;
        level.index = schedule.levels.size();
        std::vector<std::string> current(ready.begin(), ready.end());
        ready.clear();
        for (const auto& name : current) {
            emitted.insert(name);
            level.functions.push_back(work.at(name));
        }
        schedule.levels.push_back(std::move(level));
        for (const auto& name : current) {
            for (const auto& caller : callers[name]) {
                auto& count = unresolved[caller];
                if (count != 0 && --count == 0) ready.insert(caller);
            }
        }
    }

    if (emitted.size() != work.size()) {
        DependencyBuildLevel cyclic;
        cyclic.index = schedule.levels.size();
        for (const auto& [name, decision] : work)
            if (!emitted.contains(name)) cyclic.functions.push_back(decision);
        schedule.levels.push_back(std::move(cyclic));
    }
    return schedule;
}

std::string build_dependency_graph_json(const FunctionDependencyGraph& graph) {
    std::ostringstream out;
    out << "{\"version\":1,\"functions\":[";
    for (std::size_t index = 0; index < graph.functions.size(); ++index) {
        if (index != 0) out << ',';
        const auto& node = graph.functions[index];
        out << "{\"name\":"; json_string(out, node.name);
        out << ",\"callees\":[";
        for (std::size_t i = 0; i < node.callees.size(); ++i) {
            if (i != 0) out << ',';
            json_string(out, node.callees[i]);
        }
        out << "],\"callers\":[";
        for (std::size_t i = 0; i < node.callers.size(); ++i) {
            if (i != 0) out << ',';
            json_string(out, node.callers[i]);
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

std::string build_dependency_build_schedule_json(const DependencyBuildSchedule& schedule) {
    std::ostringstream out;
    out << "{\"version\":1,\"requestedWorkers\":" << schedule.requested_workers
        << ",\"workerCount\":" << schedule.worker_count << ",\"levels\":[";
    for (std::size_t index = 0; index < schedule.levels.size(); ++index) {
        if (index != 0) out << ',';
        const auto& level = schedule.levels[index];
        out << "{\"index\":" << level.index << ",\"functions\":[";
        for (std::size_t i = 0; i < level.functions.size(); ++i) {
            if (i != 0) out << ',';
            out << "{\"name\":"; json_string(out, level.functions[i].name);
            out << ",\"action\":"; json_string(out, build_action_name(level.functions[i].action));
            out << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

std::vector<std::uint8_t> assemble_incremental_artifacts(const IncrementalBuildPlan& plan,
                                                         const ArtifactCache& cache,
                                                         ArtifactAssembler assembler) {
    std::vector<FunctionArtifact> artifacts;
    for (const auto& decision : plan.functions) {
        if (decision.action == BuildAction::remove || decision.semantic_cache_key.empty()) continue;
        if (!cache.contains(decision.semantic_cache_key))
            throw std::runtime_error("missing function artifact for assembly: " + decision.name);
        artifacts.push_back({decision.name, cache.load(decision.semantic_cache_key)});
    }

    std::sort(artifacts.begin(), artifacts.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.name < rhs.name;
    });
    if (assembler) return assembler(artifacts);
    std::vector<std::uint8_t> output;
    const auto append_u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    };
    const char magic[] = {'F','R','G','A','S','M','1','\0'};
    output.insert(output.end(), std::begin(magic), std::end(magic));
    append_u64(artifacts.size());
    for (const auto& artifact : artifacts) {
        append_u64(artifact.name.size());
        output.insert(output.end(), artifact.name.begin(), artifact.name.end());
        append_u64(artifact.bytes.size());
        output.insert(output.end(), artifact.bytes.begin(), artifact.bytes.end());
    }
    return output;
}

} // namespace forge::ir
