// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "forge/diagnostics/format.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/module.hpp"
#include "forge/object/coff.hpp"
#include "forge/object/archive.hpp"
#include "forge/object/native_link.hpp"
#include "forge/object/elf.hpp"
#include "forge/pass/pipeline.hpp"
#include "forge/version.hpp"

namespace {
std::optional<std::string> read_environment(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return std::nullopt;
    std::string result(value);
    std::free(value);
    if (result.empty()) return std::nullopt;
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string(value);
#endif
}

void usage() {
    std::cerr << "Forge " << FORGE_VERSION_STRING << "\n"
              << "usage: forge <command> [options]\n\n"
              << "commands:\n"
              << "  version                 print version information\n"
              << "  verify <file.fir>       parse and verify a module\n"
              << "  print <file.fir>        print canonical Forge IR\n"
              << "  inspect <file.fir> [--stage=source|optimized|machine|all] inspect compiler stages\n"
              << "  explain <file.fir> [-O0|-O1|-O2|-O3|-Os|-Oz] explain optimization decisions\n"
              << "  doctor                  check the local Forge toolchain\n"
              << "  opt <file.fir> [-O0|-O1|-O2|-O3|-Os|-Oz] optimize and print Forge IR\n"
              << "  compile <file.fir> [-O0|-O1|-O2|-O3|-Os|-Oz] [--pass-stats] [--format=auto|elf|coff] -o <file> emit x86-64 object\n"
              << "  archive create -o <library> <objects...> create a deterministic static library\n"
              << "  link-shared [-o <library>] [--linker=<driver>] <objects...> link a native shared library\n"
              << "  new-language <name> [directory] scaffold a Forge frontend project\n"
              << "\nExecution remains available through forge-run.\n";
}

bool write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    return static_cast<bool>(output);
}

bool scaffold_language(std::string_view name, const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (fs::exists(root, error)) {
        std::cerr << "error: output directory already exists: " << root.string() << '\n';
        return false;
    }

    fs::create_directories(root / "src", error);
    fs::create_directories(root / "tests", error);
    fs::create_directories(root / "examples", error);
    if (error) {
        std::cerr << "error: cannot create " << root.string() << ": " << error.message() << '\n';
        return false;
    }

    const std::string project(name);
    const std::string cmake = "# Copyright 2026 Mario Vinciguerra\n# SPDX-License-Identifier: Apache-2.0\n\n"
        "cmake_minimum_required(VERSION 3.21)\nproject(" + project + " LANGUAGES CXX)\n"
        "find_package(Forge 2.0 CONFIG REQUIRED)\nadd_executable(" + project + " src/main.cpp)\n"
        "target_link_libraries(" + project + " PRIVATE Forge::forge)\ntarget_compile_features(" + project + " PRIVATE cxx_std_20)\n";
    const std::string main_cpp = R"CPP(// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <forge/frontend/frontend.hpp>
#include <forge/ir/context.hpp>
#include <forge/ir/printer.hpp>
#include <iostream>
#include <optional>

int main() {
    forge::ir::Context context;
    auto& module = context.create_module("example");
    forge::ir::IRBuilder builder(context, module);
    const auto function = builder.create_function_handle("main", forge::ir::i64_type());
    const auto entry = builder.create_block_handle(function, "entry");
    builder.position_at_end(entry);
    builder.create_return(builder.create_constant(forge::ir::i64_type(), "42"));
    const auto diagnostics = builder.verify();
    if (!diagnostics.empty()) return 1;
    std::cout << forge::ir::print_module(module);
}
)CPP";
    const std::string readme = "# " + project + "\n\nA language frontend created with Forge.\n\n"
        "```bash\ncmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/forge/install\n"
        "cmake --build build\n```\n";
    return write_text_file(root / "CMakeLists.txt", cmake) &&
           write_text_file(root / "src/main.cpp", main_cpp) &&
           write_text_file(root / "README.md", readme) &&
           write_text_file(root / "examples/example.txt", "# Add your language source here.\n");
}

bool read_file(const std::string& path, std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "error: cannot open " << path << '\n';
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    text = stream.str();
    return true;
}

bool print_diagnostics(const forge::Diagnostics& diagnostics, std::string_view file_name = {}, std::string_view source = {}) {
    std::cerr << forge::diagnostics::render_all(diagnostics, {file_name, source});
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity == forge::DiagnosticSeverity::error) return true;
    return false;
}

std::optional<forge::ir::Module> load_verified(const std::string& path) {
    std::string source;
    if (!read_file(path, source)) return std::nullopt;
    auto parsed = forge::ir::parse_module(source);
    if (!parsed.ok()) {
        print_diagnostics(parsed.diagnostics, path, source);
        return std::nullopt;
    }
    const auto diagnostics = forge::ir::verify_module(*parsed.module);
    if (print_diagnostics(diagnostics)) return std::nullopt;
    return std::move(*parsed.module);
}

int doctor() {
    namespace fs = std::filesystem;
    bool ok = true;
    const auto check_program = [&](std::string_view label, const char* environment, std::string_view fallback) {
        const auto configured = read_environment(environment);
        const std::string command = configured.value_or(std::string(fallback));
#if defined(_WIN32)
        const std::string probe = command + " --version >NUL 2>&1";
#else
        const std::string probe = command + " --version >/dev/null 2>&1";
#endif
        const bool found = std::system(probe.c_str()) == 0;
        std::cout << (found ? "PASS  " : "FAIL  ") << label << "  " << command << '\n';
        ok = ok && found;
    };
    std::cout << "FORGE  doctor " << FORGE_VERSION_STRING << '\n';
    std::cout << "PASS  host  "
#if defined(_WIN32)
              << "windows";
#elif defined(__APPLE__)
              << "macos";
#else
              << "linux";
#endif
    std::cout << " x86-64=" << (sizeof(void*) == 8 ? "yes" : "no") << '\n';
    check_program("cmake", "CMAKE_COMMAND", "cmake");
    check_program("c++ compiler", "CXX", "c++");
#if defined(_WIN32)
    check_program("native linker", "FORGE_LINKER", "clang++");
#else
    check_program("native linker", "FORGE_LINKER", "c++");
#endif
    const auto current = fs::current_path();
    std::error_code error;
    const auto probe = current / ".forge-doctor-write-test";
    { std::ofstream output(probe); output << "ok"; }
    const bool writable = fs::exists(probe, error);
    fs::remove(probe, error);
    std::cout << (writable ? "PASS  " : "FAIL  ") << "workspace writable  " << current.string() << '\n';
    ok = ok && writable;
    std::cout << (ok ? "FORGE  doctor passed\n" : "FORGE  doctor found blocking problems\n");
    return ok ? 0 : 1;
}

std::optional<forge::pass::OptimizationLevel> parse_level(int argc, char** argv, int first, forge::pass::OptimizationLevel fallback) {
    auto level = fallback;
    for (int index = first; index < argc; ++index) {
        const auto parsed = forge::pass::parse_optimization_level(argv[index]);
        if (!parsed) return std::nullopt;
        level = *parsed;
    }
    return level;
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string_view command = argv[1];
    if (command == "version" || command == "--version" || command == "-V") {
        std::cout << "forge " << FORGE_VERSION_STRING << '\n';
        return 0;
    }

    if (command == "--help" || command == "-h" || command == "help") {
        usage();
        return 0;
    }

    if (command == "doctor" && argc == 2) return doctor();
    if (command == "inspect" && argc >= 3) {
        std::string_view stage = "all";
        for (int index = 3; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument.rfind("--stage=", 0) == 0) stage = argument.substr(8);
            else { std::cerr << "error: unknown inspect option " << argument << '\n'; return 2; }
        }

        if (stage != "source" && stage != "optimized" && stage != "machine" && stage != "all") {
            std::cerr << "error: expected --stage=source, optimized, machine, or all\n";
            return 2;
        }
        auto module = load_verified(argv[2]);
        if (!module) return 1;
        if (stage == "source" || stage == "all")
            std::cout << "=== VERIFIED IR ===\n" << forge::ir::print_module(*module);
        forge::pass::PassManager pipeline;
        forge::pass::build_standard_pipeline(pipeline, forge::pass::OptimizationLevel::o2);
        try { (void)pipeline.run(*module); }
        catch (const std::exception& error) { std::cerr << "error: " << error.what() << '\n'; return 1; }
        if (stage == "optimized" || stage == "all")
            std::cout << "=== OPTIMIZED IR ===\n" << forge::ir::print_module(*module);
        if (stage == "machine" || stage == "all") {
            auto lowered = forge::machine::lower_module(*module);
            if (!lowered.ok()) { print_diagnostics(lowered.diagnostics); return 1; }
            std::cout << "=== MACHINE IR ===\n" << forge::machine::print_module(*lowered.module);
        }
        return 0;
    }

    if (command == "explain" && argc >= 3) {
        const auto level = parse_level(argc, argv, 3, forge::pass::OptimizationLevel::o2);
        if (!level) { std::cerr << "error: expected one optimization level\n"; return 2; }
        auto module = load_verified(argv[2]);
        if (!module) return 1;
        forge::pass::PassManager pipeline;
        forge::pass::build_standard_pipeline(pipeline, *level);
        try {
            const auto report = pipeline.run_with_report(*module);
            std::cout << "FORGE  explain -" << forge::pass::optimization_level_name(*level) << '\n';
            for (const auto& record : report.records) {
                if (!record.result.changed) continue;
                std::cout << '[' << record.pass_name << "] @" << record.function_name
                          << " rewritten=" << record.result.operations_rewritten
                          << " removed=" << record.result.operations_removed
                          << " blocks=" << record.result.blocks_removed << '\n';
            }
            std::cout << "TOTAL  rewritten=" << report.total.operations_rewritten
                      << " removed=" << report.total.operations_removed
                      << " blocks=" << report.total.blocks_removed << '\n';
        } catch (const std::exception& error) { std::cerr << "error: " << error.what() << '\n'; return 1; }
        return 0;
    }

    if (command == "new-language" && (argc == 3 || argc == 4)) {
        const std::filesystem::path output = argc == 4 ? std::filesystem::path(argv[3])
                                                       : std::filesystem::path(argv[2]);
        if (!scaffold_language(argv[2], output)) return 1;
        std::cout << "FORGE  created language frontend " << output.string() << '\n';
        return 0;
    }

    if (command == "archive" && argc >= 6 && std::string_view(argv[2]) == "create") {
        std::string output_path;
        std::vector<std::filesystem::path> inputs;
        for (int index = 3; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "-o" && index + 1 < argc) output_path = argv[++index];
            else inputs.emplace_back(argument);
        }

        if (output_path.empty() || inputs.empty()) {
            std::cerr << "error: archive create requires -o <library> and one or more object files\n";
            return 2;
        }
        auto archive = forge::object::emit_static_archive_from_files(inputs);
        if (!archive.ok()) { print_diagnostics(archive.diagnostics); return 1; }
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(archive.bytes.data()), static_cast<std::streamsize>(archive.bytes.size()));
        if (!output) { std::cerr << "error: cannot write " << output_path << '\n'; return 1; }
        std::cout << "FORGE  created static archive " << output_path
                  << " members=" << archive.stats.member_count
                  << " symbols=" << archive.stats.symbol_count
                  << " bytes=" << archive.bytes.size() << '\n';
        return 0;
    }

    if (command == "link-shared" && argc >= 5) {
        forge::object::NativeLinkOptions options;
        std::string output_path;
        std::vector<std::filesystem::path> inputs;
        for (int index = 2; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "-o" && index + 1 < argc) output_path = argv[++index];
            else if (argument.rfind("--linker=", 0) == 0) options.linker = std::string(argument.substr(9));
            else if (argument.rfind("--link-arg=", 0) == 0) options.arguments.emplace_back(argument.substr(11));
            else inputs.emplace_back(argument);
        }

        if (output_path.empty() || inputs.empty()) {
            std::cerr << "error: link-shared requires -o <library> and one or more object files\n";
            return 2;
        }
        const auto linked = forge::object::link_native_shared_library(inputs, output_path, options);
        if (!linked.ok()) { print_diagnostics(linked.diagnostics); return 1; }
        std::cout << "FORGE  linked shared library " << output_path
                  << " inputs=" << linked.input_count
                  << " bytes=" << linked.output_bytes << '\n';
        return 0;
    }

    if ((command == "verify" || command == "print") && argc == 3) {
        auto module = load_verified(argv[2]);
        if (!module) return 1;
        if (command == "print") std::cout << forge::ir::print_module(*module);
        else std::cout << "FORGE  verified " << argv[2] << '\n';
        return 0;
    }

    if (command == "compile" && argc >= 5) {
        std::string_view format = "auto";
        std::string output_path;
        auto level = forge::pass::OptimizationLevel::o2;
        bool pass_stats = false;
        for (int index = 3; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "-o" && index + 1 < argc) output_path = argv[++index];
            else if (argument.rfind("--format=", 0) == 0) format = argument.substr(9);
            else if (argument == "--pass-stats") pass_stats = true;
            else if (const auto parsed_level = forge::pass::parse_optimization_level(argument)) level = *parsed_level;
            else { std::cerr << "error: unknown compile option " << argument << '\n'; return 2; }
        }

        if (output_path.empty()) { std::cerr << "error: compile requires -o <file>\n"; return 2; }
        if (format == "auto") {
#if defined(_WIN32)
            format = "coff";
#else
            format = "elf";
#endif
        }
        auto module = load_verified(argv[2]);
        if (!module) return 1;
        forge::pass::PassManager pipeline;
        forge::pass::build_standard_pipeline(pipeline, level);
        try {
            if (pass_stats) {
                const auto report = pipeline.run_with_report(*module, false);
                std::cerr << "FORGE  pipeline -" << forge::pass::optimization_level_name(level)
                          << " passes=" << pipeline.pass_names().size()
                          << " rewritten=" << report.total.operations_rewritten
                          << " removed=" << report.total.operations_removed
                          << " blocks=" << report.total.blocks_removed << '\n';
            } else {
                (void)pipeline.run(*module, false);
            }
            // Production compilation verifies once, at the optimized-IR
            // boundary. Verifying the whole module after every pass is O(passes
            // x module) and dominated compile time on realistic inputs; the
            // per-pass checks remain available through `forge opt` for
            // diagnosing which pass broke the IR.
            const auto optimized = forge::ir::verify_module(*module);
            for (const auto& diagnostic : optimized) {
                if (diagnostic.severity == forge::DiagnosticSeverity::error) {
                    print_diagnostics(optimized);
                    return 1;
                }
            }
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 1;
        }
        auto lowered = forge::machine::lower_module(*module);
        if (!lowered.ok()) { print_diagnostics(lowered.diagnostics); return 1; }
        std::vector<std::byte> bytes;
        std::string_view label;
        if (format == "elf") {
            auto object = forge::object::emit_elf64_x86_64(*lowered.module, forge::codegen::x86_64::Abi::system_v);
            if (!object.ok()) { print_diagnostics(object.diagnostics); return 1; }
            bytes = std::move(object.bytes); label = "ELF64";
        } else if (format == "coff") {
            auto object = forge::object::emit_coff_x86_64(*lowered.module, forge::codegen::x86_64::Abi::windows);
            if (!object.ok()) { print_diagnostics(object.diagnostics); return 1; }
            bytes = std::move(object.bytes); label = "COFF AMD64";
        } else { std::cerr << "error: expected --format=auto, elf, or coff\n"; return 2; }
        std::ofstream output(output_path, std::ios::binary);
        if (!output) { std::cerr << "error: cannot create " << output_path << '\n'; return 1; }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) { std::cerr << "error: cannot write " << output_path << '\n'; return 1; }
        std::cout << "FORGE  emitted " << label << " object " << output_path << " (" << bytes.size() << " bytes)\n";
        return 0;
    }

    if (command == "opt" && (argc == 3 || argc == 4)) {
        auto level = forge::pass::OptimizationLevel::o2;
        if (argc == 4) {
            const auto parsed_level = forge::pass::parse_optimization_level(argv[3]);
            if (!parsed_level) {
                std::cerr << "error: expected -O0, -O1, -O2, -O3, -Os, or -Oz\n";
                return 2;
            }
            level = *parsed_level;
        }
        auto module = load_verified(argv[2]);
        if (!module) return 1;
        forge::pass::PassManager pipeline;
        forge::pass::build_standard_pipeline(pipeline, level);
        try {
            const auto result = pipeline.run(*module);
            (void)result;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 1;
        }

        std::cout << forge::ir::print_module(*module);
        return 0;
    }

    usage();
    return 2;
}
