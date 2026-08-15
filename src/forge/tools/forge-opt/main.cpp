// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/pass/pipeline.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: forge-opt <input.fir> [-O0|-O1|-O2|-O3|-Os|-Oz] [--stats] [--pass-timing] [--no-verify-each]\n";
        return 2;
    }
    bool print_stats = false;
    bool print_timing = false;
    bool verify_each = true;
    auto level = forge::pass::OptimizationLevel::o2;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--stats") print_stats = true;
        else if (argument == "--pass-timing") print_timing = true;
        else if (argument == "--no-verify-each") verify_each = false;
        else if (const auto parsed = forge::pass::parse_optimization_level(argument)) level = *parsed;
        else { std::cerr << "error: unknown option " << argument << '\n'; return 2; }
    }

    std::ifstream input(argv[1]);
    if (!input) { std::cerr << "error: cannot open " << argv[1] << '\n'; return 1; }
    std::ostringstream source; source << input.rdbuf();
    auto parsed = forge::ir::parse_module(source.str());
    if (!parsed.ok()) { for (const auto& d : parsed.diagnostics) std::cerr << "error: " << d.message << '\n'; return 1; }
    for (const auto& d : forge::ir::verify_module(*parsed.module))
        if (d.severity == forge::DiagnosticSeverity::error) { std::cerr << "error: " << d.message << '\n'; return 1; }

    forge::pass::PassManager pipeline;
    forge::pass::build_standard_pipeline(pipeline, level);
    try {
        const auto report = pipeline.run_with_report(*parsed.module, verify_each);
        if (print_stats || print_timing) {
            std::cerr << "FORGE  optimization complete\n"
                      << "       pipeline              -" << forge::pass::optimization_level_name(level) << '\n'
                      << "       passes                " << pipeline.pass_names().size() << '\n'
                      << "       rewritten operations " << report.total.operations_rewritten << '\n'
                      << "       removed operations   " << report.total.operations_removed << '\n'
                      << "       removed blocks       " << report.total.blocks_removed << '\n';
        }

        if (print_timing) {
            for (const auto& record : report.records) {
                const double micros = static_cast<double>(record.elapsed.count()) / 1000.0;
                std::cerr << "PASS   @" << record.function_name << "  " << record.pass_name
                          << "  " << std::fixed << std::setprecision(3) << micros << " us"
                          << "  changed=" << (record.result.changed ? 1 : 0)
                          << "  removed=" << record.result.operations_removed
                          << "  rewritten=" << record.result.operations_rewritten
                          << "  blocks=" << record.result.blocks_removed << '\n';
            }
        }
    } catch (const std::exception& error) { std::cerr << "error: " << error.what() << '\n'; return 1; }
    std::cout << forge::ir::print_module(*parsed.module);
}
