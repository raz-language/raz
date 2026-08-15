// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/build_driver.hpp"
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <sstream>
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
}

ParallelBuildSchedule build_parallel_build_schedule(const IncrementalBuildPlan& plan,
                                                     std::size_t requested_workers) {
    ParallelBuildSchedule schedule;
    schedule.requested_workers = requested_workers;
    std::vector<FunctionBuildDecision> work;
    for (const auto& decision : plan.functions)
        if (decision.action != BuildAction::remove) work.push_back(decision);
    std::sort(work.begin(), work.end(), [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
    schedule.worker_count = resolve_workers(requested_workers, work.size());
    schedule.shards.resize(schedule.worker_count);
    for (std::size_t index = 0; index < schedule.shards.size(); ++index) schedule.shards[index].index = index;
    for (std::size_t index = 0; index < work.size(); ++index)
        schedule.shards[index % schedule.worker_count].functions.push_back(std::move(work[index]));
    return schedule;
}

std::string build_parallel_build_schedule_json(const ParallelBuildSchedule& schedule) {
    std::ostringstream out;
    out << "{\"version\":1,\"requestedWorkers\":" << schedule.requested_workers
        << ",\"workerCount\":" << schedule.worker_count << ",\"shards\":[";
    for (std::size_t shard_index = 0; shard_index < schedule.shards.size(); ++shard_index) {
        if (shard_index != 0) out << ',';
        const auto& shard = schedule.shards[shard_index];
        out << "{\"index\":" << shard.index << ",\"functions\":[";
        for (std::size_t index = 0; index < shard.functions.size(); ++index) {
            if (index != 0) out << ',';
            out << "{\"name\":"; json_string(out, shard.functions[index].name);
            out << ",\"action\":"; json_string(out, build_action_name(shard.functions[index].action));
            out << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

std::string build_execution_summary_json(const BuildExecutionSummary& summary) {
    std::ostringstream out;
    out << "{\"version\":1,\"workerCount\":" << summary.worker_count
        << ",\"cacheHits\":" << summary.cache_hits
        << ",\"cacheMisses\":" << summary.cache_misses
        << ",\"rebuilt\":" << summary.rebuilt
        << ",\"reused\":" << summary.reused
        << ",\"frontendRefreshed\":" << summary.frontend_refreshed
        << ",\"removed\":" << summary.removed
        << ",\"failed\":" << summary.failed
        << ",\"artifactBytes\":" << summary.artifact_bytes << ",\"functions\":[";
    for (std::size_t index = 0; index < summary.functions.size(); ++index) {
        if (index != 0) out << ',';
        const auto& function = summary.functions[index];
        out << "{\"name\":"; json_string(out, function.name);
        out << ",\"action\":"; json_string(out, build_action_name(function.action));
        out << ",\"cacheHit\":" << (function.cache_hit ? "true" : "false")
            << ",\"succeeded\":" << (function.succeeded ? "true" : "false")
            << ",\"artifactBytes\":" << function.artifact_bytes << ",\"message\":";
        json_string(out, function.message);
        out << '}';
    }
    out << "]}";
    return out.str();
}

BuildExecutionSummary execute_incremental_build_plan(const IncrementalBuildPlan& plan,
                                                     ArtifactCache& cache,
                                                     FunctionCompiler compiler,
                                                     FrontendRefresher frontend_refresher,
                                                     std::size_t requested_workers) {
    BuildExecutionSummary summary;
    auto schedule = build_parallel_build_schedule(plan, requested_workers);
    summary.worker_count = schedule.worker_count;
    for (const auto& decision : plan.functions) {
        if (decision.action != BuildAction::remove) continue;
        const bool removed_semantic = decision.semantic_cache_key.empty() || cache.erase(decision.semantic_cache_key);
        const bool removed_frontend = decision.frontend_cache_key.empty() ||
            decision.frontend_cache_key == decision.semantic_cache_key || cache.erase(decision.frontend_cache_key);
        summary.functions.push_back({decision.name, decision.action, false,
                                     removed_semantic && removed_frontend, 0,
                                     removed_semantic && removed_frontend ? "removed" : "cache entry absent"});
        ++summary.removed;
    }
    std::mutex mutex;
    std::vector<std::thread> threads;
    threads.reserve(schedule.shards.size());
    for (const auto& shard : schedule.shards) {
        threads.emplace_back([&, shard] {
            std::vector<FunctionBuildResult> local;
            for (const auto& decision : shard.functions) {
                FunctionBuildResult result;
                result.name = decision.name;
                result.action = decision.action;
                try {
                    if (decision.action == BuildAction::frontend_refresh) {
                        if (frontend_refresher) frontend_refresher(decision);
                        result.succeeded = true;
                        result.message = "frontend state refreshed";
                    } else {
                        const auto& key = decision.semantic_cache_key;
                        if (!key.empty() && cache.contains(key)) {
                            const auto bytes = cache.load(key);
                            result.cache_hit = true;
                            result.succeeded = true;
                            result.artifact_bytes = bytes.size();
                            result.message = "cache hit";
                        } else if (decision.action == BuildAction::reuse) {
                            result.succeeded = false;
                            result.message = "required reusable artifact is missing";
                        } else if (!compiler) {
                            result.succeeded = false;
                            result.message = "no function compiler callback";
                        } else {
                            auto bytes = compiler(decision);
                            if (!key.empty()) cache.store(key, bytes);
                            result.succeeded = true;
                            result.artifact_bytes = bytes.size();
                            result.message = "rebuilt";
                        }
                    }
                } catch (const std::exception& error) {
                    result.succeeded = false;
                    result.message = error.what();
                } catch (...) {
                    result.succeeded = false;
                    result.message = "unknown build failure";
                }
                local.push_back(std::move(result));
            }
            std::lock_guard lock(mutex);
            for (auto& result : local) summary.functions.push_back(std::move(result));
        });
    }

    for (auto& thread : threads) thread.join();
    std::sort(summary.functions.begin(), summary.functions.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.name < rhs.name;
    });
    for (const auto& result : summary.functions) {
        summary.artifact_bytes += result.artifact_bytes;
        if (!result.succeeded) ++summary.failed;
        if (result.cache_hit) ++summary.cache_hits;
        else if (result.action == BuildAction::rebuild) ++summary.cache_misses;
        if (result.action == BuildAction::rebuild && result.succeeded && !result.cache_hit) ++summary.rebuilt;
        if (result.action == BuildAction::reuse && result.succeeded) ++summary.reused;
        if (result.action == BuildAction::frontend_refresh && result.succeeded) ++summary.frontend_refreshed;
    }
    return summary;
}

} // namespace forge::ir
