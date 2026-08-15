// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "forge/diagnostics/format.hpp"
#include "forge/interpreter/interpreter.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/jit/engine.hpp"
#include "forge/jit/invoke.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/verifier.hpp"

namespace {

enum class EngineMode { interpreter, jit, compare };

struct Options {
    EngineMode engine{EngineMode::interpreter};
    std::string file;
    std::string function;
    std::vector<std::string> arguments;
};

void print_usage() {
    std::cerr << "usage: forge-run [--engine=interpreter|jit|compare] "
                 "<file.fir> <function> [integer arguments...]\n";
}

std::optional<EngineMode> parse_engine(std::string_view value) {
    if (value == "interpreter") return EngineMode::interpreter;
    if (value == "jit") return EngineMode::jit;
    if (value == "compare") return EngineMode::compare;
    return std::nullopt;
}

std::optional<Options> parse_options(int argc, char** argv) {
    Options options;
    std::vector<std::string> positional;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        constexpr std::string_view prefix = "--engine=";
        if (argument.starts_with(prefix)) {
            const auto mode = parse_engine(argument.substr(prefix.size()));
            if (!mode) {
                std::cerr << "error: unknown execution engine '" << argument.substr(prefix.size()) << "'\n";
                return std::nullopt;
            }
            options.engine = *mode;
        } else if (argument == "--help" || argument == "-h") {
            print_usage();
            std::exit(0);
        } else if (argument.starts_with("--")) {
            std::cerr << "error: unknown option '" << argument << "'\n";
            return std::nullopt;
        } else {
            positional.emplace_back(argument);
        }
    }

    if (positional.size() < 2) return std::nullopt;
    options.file = std::move(positional[0]);
    options.function = std::move(positional[1]);
    options.arguments.assign(positional.begin() + 2, positional.end());
    return options;
}

void print_diagnostics(const forge::Diagnostics& diagnostics) {
    std::cerr << forge::diagnostics::render_all(diagnostics);
}

const forge::ir::Function* find_function(const forge::ir::Module& module, std::string_view name) {
    for (const auto& function : module.functions()) {
        if (function.name == name) return &function;
    }
    return nullptr;
}

std::uint64_t type_mask(forge::ir::Type type) {
    switch (type.kind()) {
        case forge::ir::TypeKind::i1: return 0x1ULL;
        case forge::ir::TypeKind::i8: return 0xffULL;
        case forge::ir::TypeKind::i16: return 0xffffULL;
        case forge::ir::TypeKind::i32: return 0xffffffffULL;
        case forge::ir::TypeKind::i64: return ~0ULL;
        default: return 0;
    }
}

std::optional<std::uint64_t> parse_integer(std::string_view text) {
    if (text.empty()) return std::nullopt;
    if (text.front() == '-') {
        std::int64_t signed_value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), signed_value, 10);
        if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
        return static_cast<std::uint64_t>(signed_value);
    }
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
        base = 16;
        if (text.empty()) return std::nullopt;
    }
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::optional<std::vector<std::uint64_t>> parse_arguments(
    const forge::ir::Function& function, const std::vector<std::string>& texts) {
    if (texts.size() != function.parameters.size()) {
        std::cerr << "error: @" << function.name << " expects " << function.parameters.size()
                  << " argument(s), but " << texts.size() << " were provided\n";
        return std::nullopt;
    }
    std::vector<std::uint64_t> values;
    values.reserve(texts.size());
    for (std::size_t index = 0; index < texts.size(); ++index) {
        const auto type = function.parameters[index].type;
        if (!type.is_integer()) {
            std::cerr << "error: command-line execution currently supports only integer parameters; parameter "
                      << index << " has type " << type.str() << '\n';
            return std::nullopt;
        }
        const auto parsed = parse_integer(texts[index]);
        if (!parsed) {
            std::cerr << "error: invalid integer argument '" << texts[index] << "'\n";
            return std::nullopt;
        }
        values.push_back(*parsed & type_mask(type));
    }
    return values;
}

struct ExecutionResult {
    bool is_void{};
    forge::ir::Type type;
    std::uint64_t bits{};
};

std::optional<ExecutionResult> run_interpreter(
    const forge::ir::Module& module,
    const forge::ir::Function& function,
    std::span<const std::uint64_t> raw_arguments) {
    std::vector<forge::interpreter::Value> arguments;
    arguments.reserve(raw_arguments.size());
    for (std::size_t index = 0; index < raw_arguments.size(); ++index) {
        arguments.push_back(forge::interpreter::Value::integer(
            function.parameters[index].type, raw_arguments[index]));
    }
    auto result = forge::interpreter::execute(module, function.name, arguments);
    if (!result.diagnostics.empty()) {
        print_diagnostics(result.diagnostics);
        return std::nullopt;
    }

    if (!result.value || result.value->kind() == forge::interpreter::Value::Kind::void_) {
        return ExecutionResult{true, forge::ir::Type(forge::ir::TypeKind::void_), 0};
    }

    if (result.value->kind() != forge::interpreter::Value::Kind::integer) {
        std::cerr << "error: command-line execution cannot print a pointer result\n";
        return std::nullopt;
    }
    return ExecutionResult{false, result.value->type(), result.value->bits() & type_mask(result.value->type())};
}

#if defined(_WIN32)
constexpr auto host_abi = forge::codegen::x86_64::Abi::windows;
#else
constexpr auto host_abi = forge::codegen::x86_64::Abi::system_v;
#endif

std::optional<ExecutionResult> run_jit(
    const forge::ir::Module& module,
    const forge::ir::Function& function,
    std::span<const std::uint64_t> arguments) {
#if !(defined(__x86_64__) || defined(_M_X64) || defined(__amd64__))
    (void)module;
    (void)function;
    (void)arguments;
    std::cerr << "error: the JIT currently requires an x86-64 host\n";
    return std::nullopt;
#else
    if (!(function.return_type.is_integer() || function.return_type.kind() == forge::ir::TypeKind::void_)) {
        std::cerr << "error: command-line JIT execution currently supports only integer or void returns\n";
        return std::nullopt;
    }
    auto lowered = forge::machine::lower_module(module);
    if (!lowered.ok()) {
        print_diagnostics(lowered.diagnostics);
        return std::nullopt;
    }
    const auto verification = forge::machine::verify_module(*lowered.module);
    if (!verification.empty()) {
        print_diagnostics(verification);
        return std::nullopt;
    }
    auto loaded = forge::jit::load(*lowered.module, host_abi);
    if (!loaded.ok()) {
        print_diagnostics(loaded.diagnostics);
        return std::nullopt;
    }
    void* address = loaded.engine->lookup(function.name);
    if (!address) {
        std::cerr << "error: JIT entry @" << function.name << " was not found\n";
        return std::nullopt;
    }
    const bool returns_void = function.return_type.kind() == forge::ir::TypeKind::void_;
    const auto invocation = forge::jit::invoke_integer(address, arguments, returns_void);
    if (!invocation.ok()) {
        print_diagnostics(invocation.diagnostics);
        return std::nullopt;
    }

    if (returns_void) return ExecutionResult{true, function.return_type, 0};
    return ExecutionResult{false, function.return_type, invocation.bits & type_mask(function.return_type)};
#endif
}

void print_result(const ExecutionResult& result) {
    if (result.is_void) {
        std::cout << "void\n";
        return;
    }
    const auto width_mask = type_mask(result.type);
    const auto bits = result.bits & width_mask;
    std::int64_t signed_value{};
    switch (result.type.kind()) {
        case forge::ir::TypeKind::i1: signed_value = static_cast<std::int64_t>(bits); break;
        case forge::ir::TypeKind::i8: signed_value = static_cast<std::int8_t>(bits); break;
        case forge::ir::TypeKind::i16: signed_value = static_cast<std::int16_t>(bits); break;
        case forge::ir::TypeKind::i32: signed_value = static_cast<std::int32_t>(bits); break;
        case forge::ir::TypeKind::i64: signed_value = static_cast<std::int64_t>(bits); break;
        default: signed_value = 0; break;
    }
    std::cout << signed_value << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    if (!options) {
        print_usage();
        return 2;
    }

    std::ifstream input(options->file, std::ios::binary);
    if (!input) {
        std::cerr << "error: cannot open " << options->file << '\n';
        return 1;
    }

    const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto parsed = forge::ir::parse_module(source);
    if (!parsed.module) {
        print_diagnostics(parsed.diagnostics);
        return 1;
    }
    const auto verification = forge::ir::verify_module(*parsed.module);
    if (!verification.empty()) {
        print_diagnostics(verification);
        return 1;
    }
    const auto* function = find_function(*parsed.module, options->function);
    if (!function || function->is_external) {
        std::cerr << "error: executable function @" << options->function << " was not found\n";
        return 1;
    }
    const auto arguments = parse_arguments(*function, options->arguments);
    if (!arguments) return 2;

    if (options->engine == EngineMode::interpreter) {
        const auto result = run_interpreter(*parsed.module, *function, *arguments);
        if (!result) return 1;
        print_result(*result);
        return 0;
    }

    if (options->engine == EngineMode::jit) {
        const auto result = run_jit(*parsed.module, *function, *arguments);
        if (!result) return 1;
        print_result(*result);
        return 0;
    }

    const auto interpreted = run_interpreter(*parsed.module, *function, *arguments);
    if (!interpreted) return 1;
    const auto native = run_jit(*parsed.module, *function, *arguments);
    if (!native) return 1;
    if (interpreted->is_void != native->is_void || interpreted->type != native->type ||
        interpreted->bits != native->bits) {
        std::cerr << "error: execution mismatch for @" << function->name
                  << ": interpreter=" << interpreted->bits << ", JIT=" << native->bits << '\n';
        return 1;
    }

    print_result(*native);
    std::cerr << "compare: interpreter and JIT agree\n";
    return 0;
}
