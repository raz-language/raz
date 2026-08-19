// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge-c/forge.h"

#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

std::string i64_ascii(std::int64_t handle, std::int64_t length) {
    if (handle == 0 || length <= 0) return {};
    const auto* values = reinterpret_cast<const std::int64_t*>(static_cast<std::uintptr_t>(handle));
    std::string text;
    text.reserve(static_cast<std::size_t>(length));
    for (std::int64_t index = 0; index < length; ++index)
        text.push_back(static_cast<char>(values[index] & 0xff));
    return text;
}

std::int64_t write_i64_ascii(const std::string& text, std::int64_t handle, std::int64_t capacity) {
    if (handle == 0 || capacity < 0 || static_cast<std::uint64_t>(capacity) < text.size()) return -1;
    auto* values = reinterpret_cast<std::int64_t*>(static_cast<std::uintptr_t>(handle));
    for (std::size_t index = 0; index < text.size(); ++index)
        values[index] = static_cast<unsigned char>(text[index]);
    return static_cast<std::int64_t>(text.size());
}

std::string environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

std::string shell_quote(const std::filesystem::path& value) {
#if defined(_WIN32)
    const std::string text = value.string();
    std::string result = "\"";
    for (const char c : text) result += c == '"' ? "\\\"" : std::string(1, c);
    return result + "\"";
#else
    std::string result = "'";
    for (const char c : value.string()) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
#endif
}

int execute_shell_command(const std::string& command) {
#if defined(_WIN32)
    return std::system((std::string("\"") + command + "\"").c_str());
#else
    return std::system(command.c_str());
#endif
}

std::filesystem::path current_executable_path() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return {};
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
#elif defined(__linux__)
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path;
#else
    return {};
#endif
}

std::filesystem::path oblink_executable_path() {
    const std::string configured = environment_value("RAZ_LINKER");
    if (!configured.empty()) return std::filesystem::path(configured);

#if defined(_WIN32)
    // Installed toolchains keep ObLink beside raz.exe/razc.exe. Prefer that
    // relocatable sibling over the CMake build-tree path compiled into the
    // bootstrap bridge; otherwise moving/installing Raz would leave a stale
    // absolute linker path behind.
    const auto executable = current_executable_path();
    if (!executable.empty()) {
        const auto sibling = executable.parent_path() / "oblink.exe";
        if (std::filesystem::is_regular_file(sibling)) return sibling;
    }
#ifdef RAZ_OBLINK_PATH
    const std::filesystem::path build_oblink(RAZ_OBLINK_PATH);
    if (std::filesystem::is_regular_file(build_oblink)) return build_oblink;
#endif
    return std::filesystem::path("oblink.exe");
#else
    return std::filesystem::path("c++");
#endif
}

std::filesystem::path runtime_library_path() {
    const std::string configured = environment_value("RAZ_RUNTIME_LIBRARY");
    if (!configured.empty()) return std::filesystem::path(configured);

    const auto executable = current_executable_path();
    if (!executable.empty()) {
#if defined(_WIN32)
        const auto installed = executable.parent_path().parent_path() / "lib" / "raz_runtime.lib";
#else
        const auto installed = executable.parent_path().parent_path() / "lib" / "libraz_runtime.a";
#endif
        if (std::filesystem::is_regular_file(installed)) return installed;
    }
#ifdef RAZ_RUNTIME_LIBRARY_PATH
    const std::filesystem::path configured_build(RAZ_RUNTIME_LIBRARY_PATH);
    if (std::filesystem::is_regular_file(configured_build)) return configured_build;
#endif
    return {};
}

[[maybe_unused]] bool windows_msvc_style_driver(std::string linker) {
#if defined(_WIN32)
    std::replace(linker.begin(), linker.end(), '\\', '/');
    const auto slash = linker.find_last_of('/');
    if (slash != std::string::npos) linker.erase(0, slash + 1);
    std::transform(linker.begin(), linker.end(), linker.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return linker == "cl" || linker == "cl.exe" || linker == "clang-cl" || linker == "clang-cl.exe";
#else
    (void)linker;
    return false;
#endif
}

#if defined(_WIN32)
std::string windows_native_library_environment(std::string value) {
    std::string result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(';', begin);
        const std::string part = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        std::string lower = part;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool foreign = lower.find("strawberry") != std::string::npos ||
                             lower.find("mingw") != std::string::npos ||
                             lower.find("msys") != std::string::npos;
        if (!part.empty() && !foreign) {
            if (!result.empty()) result.push_back(';');
            result += part;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

int execute_windows_msvc_fallback(const std::string& command) {
    const std::string prior_lib = environment_value("LIB");
    const std::string prior_libpath = environment_value("LIBPATH");
    _putenv_s("LIB", windows_native_library_environment(prior_lib).c_str());
    _putenv_s("LIBPATH", windows_native_library_environment(prior_libpath).c_str());
    const int status = execute_shell_command(command);
    _putenv_s("LIB", prior_lib.c_str());
    _putenv_s("LIBPATH", prior_libpath.c_str());
    return status;
}
#endif

[[maybe_unused]] void append_optional_link_library(std::ostringstream& command, const char* path) {
    if (path == nullptr || *path == '\0') return;
    const std::filesystem::path value(path);
    if (std::filesystem::is_regular_file(value)) command << ' ' << shell_quote(value);
}

forge_optimization_level_t forge_optimization(std::int64_t level) {
    if (level <= 0) return FORGE_OPT_O0;
    if (level == 1) return FORGE_OPT_O1;
    if (level == 2) return FORGE_OPT_O2;
    if (level == 3) return FORGE_OPT_O3;
    if (level == 4) return FORGE_OPT_OS;
    if (level == 5) return FORGE_OPT_OZ;
    return FORGE_OPT_O2;
}

forge_native_abi_t host_abi() {
#if defined(_WIN32)
    return FORGE_ABI_WINDOWS_X64;
#else
    return FORGE_ABI_SYSTEM_V_X86_64;
#endif
}

std::filesystem::path object_sidecar(std::string path) {
    std::filesystem::path result(std::move(path));
#if defined(_WIN32)
    result.replace_extension(".obj");
#else
    result.replace_extension(".o");
#endif
    return result;
}

void print_forge_error(const char* prefix) {
    std::cerr << prefix << forge_last_error();
    const std::string error = forge_last_error();
    if (error.empty() || error.back() != '\n') std::cerr << '\n';
}

void write_forge_phase_profile(std::chrono::steady_clock::duration optimize,
                               std::chrono::steady_clock::duration object_emit) {
    const char* path = std::getenv("RAZ_FORGE_PHASE_PROFILE");
    if (path == nullptr || *path == '\0') return;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    const auto optimize_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(optimize).count();
    const auto object_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(object_emit).count();
    out << "forge_optimize_ns " << optimize_ns << '\n';
    out << "forge_object_emit_ns " << object_ns << '\n';
}

std::int64_t emit_module_object(forge_module_t* module, const std::string& output_path_text,
                                std::int64_t optimization_level) {
    if (module == nullptr || output_path_text.empty()) return 0;
    const auto dump_module = [&](const char* suffix) {
        if (std::getenv("RAZ_FORGE_DEBUG_DUMP") == nullptr) return;
        const std::size_t required = forge_module_print(module, nullptr, 0);
        if (required == 0) return;
        std::vector<char> text(required);
        forge_module_print(module, text.data(), text.size());
        std::ofstream dump(output_path_text + suffix, std::ios::binary | std::ios::trunc);
        if (dump) dump.write(text.data(), static_cast<std::streamsize>(required > 0 ? required - 1 : 0));
    };
    dump_module(".structured.before.fir");
    const auto optimize_start = std::chrono::steady_clock::now();
    if (!forge_module_optimize(module, forge_optimization(optimization_level))) {
        print_forge_error("raz: Forge optimization failed:\n");
        dump_module(".structured.optimize-failed.fir");
        return 0;
    }
    const auto optimize_end = std::chrono::steady_clock::now();
    dump_module(".structured.after.fir");

    const auto abi = host_abi();
    const auto object_path = object_sidecar(output_path_text);
    const auto object_start = std::chrono::steady_clock::now();
    const std::size_t written = forge_module_write_object_file(module, abi, object_path.string().c_str());
    const auto object_end = std::chrono::steady_clock::now();
    write_forge_phase_profile(optimize_end - optimize_start, object_end - object_start);
    if (written == 0) {
        print_forge_error("raz: Forge object emission failed:\n");
        return 0;
    }
    return static_cast<std::int64_t>(written);
}

struct RazForgeSession {
    forge_context_t* context{};
    forge_module_t* module{};
    std::vector<forge_function_t*> functions;
    std::vector<forge_block_t*> blocks;
    std::vector<std::string> array_names;

    ~RazForgeSession() {
        for (auto* block : blocks) forge_block_destroy(block);
        for (auto* function : functions) forge_function_destroy(function);
        forge_module_destroy(module);
        forge_context_destroy(context);
    }
};

template <typename T>
std::int64_t pointer_handle(T* pointer) {
    return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

template <typename T>
T* from_handle(std::int64_t handle) {
    return reinterpret_cast<T*>(static_cast<std::uintptr_t>(handle));
}

forge_type_kind_t forge_type(std::int64_t type) {
    if (type < FORGE_TYPE_VOID || type > FORGE_TYPE_PTR) return FORGE_TYPE_VOID;
    return static_cast<forge_type_kind_t>(type);
}

forge_aggregate_ref_kind_t forge_aggregate(std::int64_t kind) {
    if (kind < FORGE_AGGREGATE_SCALAR || kind > FORGE_AGGREGATE_ARRAY) return FORGE_AGGREGATE_SCALAR;
    return static_cast<forge_aggregate_ref_kind_t>(kind);
}

forge_borrow_mode_t forge_borrow(std::int64_t mode) {
    if (mode < FORGE_BORROW_NONE || mode > FORGE_BORROW_MUTABLE) return FORGE_BORROW_NONE;
    return static_cast<forge_borrow_mode_t>(mode);
}

} // namespace

// Resolve default project-native artifact paths at the platform ABI boundary.
// Raz owns package/profile semantics. This bridge owns only host filesystem
// spelling (directory separators and native file suffixes) and directory creation.
extern "C" std::int64_t raz_compiler_project_native_path_i64(
    std::int64_t manifest_path_handle,
    std::int64_t manifest_path_length,
    std::int64_t package_name_handle,
    std::int64_t package_name_length,
    std::int64_t release_profile,
    std::int64_t object_path,
    std::int64_t output_handle,
    std::int64_t output_capacity) {
    const std::string manifest_text = i64_ascii(manifest_path_handle, manifest_path_length);
    const std::string package_name = i64_ascii(package_name_handle, package_name_length);
    if (manifest_text.empty() || package_name.empty() || output_handle == 0 || output_capacity <= 0)
        return -1;
    if (package_name.find('/') != std::string::npos || package_name.find('\\') != std::string::npos)
        return -1;

    const std::filesystem::path manifest(manifest_text);
    const std::filesystem::path root = manifest.parent_path();
    const std::filesystem::path profile = release_profile != 0 ? "release" : "debug";
    const std::filesystem::path profile_root = root / "target" / profile;

    std::filesystem::path output;
    if (object_path != 0) {
#if defined(_WIN32)
        output = profile_root / "native" / (package_name + ".obj");
#else
        output = profile_root / "native" / (package_name + ".o");
#endif
    } else {
#if defined(_WIN32)
        output = profile_root / (package_name + ".exe");
#else
        output = profile_root / package_name;
#endif
    }

    std::error_code error;
    std::filesystem::create_directories(output.parent_path(), error);
    if (error) {
        std::cerr << "raz: could not create native output directory: " << error.message() << '\n';
        return -1;
    }
    return write_i64_ascii(output.string(), output_handle, output_capacity);
}

extern "C" std::int64_t raz_compiler_forge_emit_object_i64(
    std::int64_t ir_handle,
    std::int64_t ir_length,
    std::int64_t output_path_handle,
    std::int64_t output_path_length,
    std::int64_t optimization_level) {
    if (ir_handle == 0 || ir_length <= 0 || output_path_handle == 0 || output_path_length <= 0) {
        std::cerr << "raz: invalid in-process Forge object-emission request\n";
        return 0;
    }

    const std::string ir = i64_ascii(ir_handle, ir_length);
    const std::string output_path_text = i64_ascii(output_path_handle, output_path_length);
    if (ir.empty() || output_path_text.empty()) {
        std::cerr << "raz: failed to materialize in-process Forge request\n";
        return 0;
    }

    forge_context_t* context = forge_context_create();
    if (context == nullptr) {
        print_forge_error("raz: Forge context creation failed: ");
        return 0;
    }

    forge_module_t* module = forge_module_parse(context, ir.data(), ir.size());
    if (module == nullptr) {
        print_forge_error("raz: Forge IR parse failed:\n");
        forge_context_destroy(context);
        return 0;
    }

    const auto result = emit_module_object(module, output_path_text, optimization_level);
    forge_module_destroy(module);
    forge_context_destroy(context);
    return result;
}

extern "C" std::int64_t raz_compiler_forge_link_executable_i64(
    std::int64_t object_path_handle,
    std::int64_t object_path_length,
    std::int64_t output_path_handle,
    std::int64_t output_path_length) {
    const std::string object_text = i64_ascii(object_path_handle, object_path_length);
    const std::string output_text = i64_ascii(output_path_handle, output_path_length);
    if (object_text.empty() || output_text.empty()) return 0;

    const std::filesystem::path object(object_text);
    const std::filesystem::path output(output_text);
    const std::filesystem::path runtime = runtime_library_path();
    if (!std::filesystem::is_regular_file(object)) {
        std::cerr << "raz: native object is missing: " << object.string() << '\n';
        return 0;
    }
    if (runtime.empty() || !std::filesystem::is_regular_file(runtime)) {
        std::cerr << "raz: Raz runtime library is unavailable; set RAZ_RUNTIME_LIBRARY\n";
        return 0;
    }

    const std::string configured = environment_value("RAZ_LINKER");
    const std::string linker = oblink_executable_path().string();
    std::ostringstream command;

#if defined(_WIN32)
    const auto linker_file = std::filesystem::path(linker).filename().string();
    const bool use_oblink = linker_file == "oblink" || linker_file == "oblink.exe";
    if (use_oblink) {
        command << shell_quote(std::filesystem::path(linker)) << ' '
                << shell_quote(object) << ' ' << shell_quote(runtime);
        // raz_runtime is compiled against OpenSSL when it is available, and a
        // static archive cannot carry those dependencies forward. Every other
        // branch below passes them; omitting them here left ObLink resolving
        // EVP_* against nothing on any image that touches the crypto runtime.
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
        append_optional_link_library(command, RAZ_OPENSSL_SSL_LIBRARY_PATH);
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
        append_optional_link_library(command, RAZ_OPENSSL_CRYPTO_LIBRARY_PATH);
#endif
        command << " -l ws2_32 -l bcrypt -l crypt32";
        if (output.filename() == "raz-compiler.exe" || output.filename() == "raz-compiler")
            command << " --stack 8388608";
        command << " -o " << shell_quote(output);
    } else if (windows_msvc_style_driver(linker)) {
        command << shell_quote(std::filesystem::path(linker)) << " /nologo "
                << shell_quote(object) << ' ' << shell_quote(runtime);
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
        append_optional_link_library(command, RAZ_OPENSSL_SSL_LIBRARY_PATH);
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
        append_optional_link_library(command, RAZ_OPENSSL_CRYPTO_LIBRARY_PATH);
#endif
        command << " ws2_32.lib bcrypt.lib crypt32.lib /Fe:" << shell_quote(output)
                << " /link /STACK:8388608";
    } else {
        command << shell_quote(std::filesystem::path(linker)) << ' '
                << shell_quote(object) << ' ' << shell_quote(runtime);
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
        append_optional_link_library(command, RAZ_OPENSSL_SSL_LIBRARY_PATH);
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
        append_optional_link_library(command, RAZ_OPENSSL_CRYPTO_LIBRARY_PATH);
#endif
        command << " -o " << shell_quote(output) << " -lws2_32 -lbcrypt -lcrypt32";
    }
#else
    command << shell_quote(std::filesystem::path(linker)) << ' '
            << shell_quote(object) << ' ' << shell_quote(runtime);
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
    append_optional_link_library(command, RAZ_OPENSSL_SSL_LIBRARY_PATH);
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
    append_optional_link_library(command, RAZ_OPENSSL_CRYPTO_LIBRARY_PATH);
#endif
    command << " -o " << shell_quote(output) << " -pthread";
#endif

    const std::string extra = environment_value("RAZ_NATIVE_LINK_ARGS");
    if (!extra.empty()) command << ' ' << extra;
    if (execute_shell_command(command.str()) != 0) {
#if defined(_WIN32)
        if (use_oblink && configured.empty()) {
            const std::string fallback_configured = environment_value("RAZ_EXTERNAL_LINKER");
            const std::string fallback = fallback_configured.empty() ? "clang-cl" : fallback_configured;
            std::ostringstream retry;
            if (windows_msvc_style_driver(fallback)) {
                // Keep the fallback in the same ABI/CRT world as the CMake-built
                // runtime.  clang-cl/cl use MSVC link syntax; passing GNU -l...
                // options here used to silently drop the Windows system libs and,
                // worse, the Strawberry/MinGW OpenSSL archives could pull an
                // incompatible msvcrt into the image.
                retry << shell_quote(std::filesystem::path(fallback)) << " /nologo /MD "
                      << shell_quote(object) << ' ' << shell_quote(runtime)
                      << " ws2_32.lib bcrypt.lib crypt32.lib /Fe:" << shell_quote(output)
                      << " /link /STACK:8388608";
            } else {
                retry << shell_quote(std::filesystem::path(fallback)) << ' '
                      << shell_quote(object) << ' ' << shell_quote(runtime)
                      << " -o " << shell_quote(output) << " -lws2_32 -lbcrypt -lcrypt32";
            }
            const int retry_status = windows_msvc_style_driver(fallback)
                ? execute_windows_msvc_fallback(retry.str())
                : execute_shell_command(retry.str());
            if (retry_status == 0) return std::filesystem::is_regular_file(output) ? 1 : 0;
        }
#endif
        std::cerr << "raz: native link failed (ObLink and fallback linker both failed)\n";
        return 0;
    }
    return std::filesystem::is_regular_file(output) ? 1 : 0;
}

// Generic i64-friendly structured bridge. Raz source owns the MIR -> Forge lowering;
// this layer only converts stage-arena strings/handles into the stable Forge C ABI.
extern "C" std::int64_t raz_compiler_forge_session_create_i64(
    std::int64_t name_handle, std::int64_t name_length) {
    auto session = std::make_unique<RazForgeSession>();
    session->context = forge_context_create();
    const std::string name = i64_ascii(name_handle, name_length);
    session->module = session->context ? forge_module_create(session->context, name.empty() ? "raz" : name.c_str()) : nullptr;
    if (!session->context || !session->module) return 0;
    return pointer_handle(session.release());
}

extern "C" void raz_compiler_forge_session_destroy_i64(std::int64_t session_handle) {
    delete from_handle<RazForgeSession>(session_handle);
}

extern "C" std::int64_t raz_compiler_forge_session_add_struct_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t move_only) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    if (name.empty()) return 0;
    const std::size_t index = forge_module_add_struct(session->module, name.c_str(), move_only != 0);
    if (index == SIZE_MAX) return 0;
    return static_cast<std::int64_t>(index + 1);
}

extern "C" std::int64_t raz_compiler_forge_session_add_array_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t element_type, std::int64_t element_count, std::int64_t move_only) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module || element_count <= 0 || element_count > UINT32_MAX) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    if (name.empty()) return 0;
    for (std::size_t index = 0; index < session->array_names.size(); ++index) {
        if (session->array_names[index] == name) return static_cast<std::int64_t>(index + 1);
    }
    const std::size_t index = forge_module_add_array(
        session->module, name.c_str(), forge_type(element_type),
        static_cast<std::uint32_t>(element_count), move_only != 0);
    if (index == SIZE_MAX) return 0;
    session->array_names.push_back(name);
    return static_cast<std::int64_t>(session->array_names.size());
}

extern "C" std::int64_t raz_compiler_forge_session_add_aggregate_array_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t element_aggregate_kind, std::int64_t element_aggregate_name_handle,
    std::int64_t element_aggregate_name_length, std::int64_t element_count,
    std::int64_t move_only) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module || element_count <= 0 || element_count > UINT32_MAX) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    std::string aggregate_name = i64_ascii(element_aggregate_name_handle, element_aggregate_name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    if (!aggregate_name.empty() && aggregate_name.front() == '@') aggregate_name.erase(aggregate_name.begin());
    if (name.empty() || aggregate_name.empty()) return 0;
    for (std::size_t index = 0; index < session->array_names.size(); ++index) {
        if (session->array_names[index] == name) return static_cast<std::int64_t>(index + 1);
    }
    const auto kind = element_aggregate_kind == 1 ? FORGE_AGGREGATE_STRUCT :
                      element_aggregate_kind == 2 ? FORGE_AGGREGATE_ARRAY : FORGE_AGGREGATE_SCALAR;
    if (kind == FORGE_AGGREGATE_SCALAR) return 0;
    const std::size_t index = forge_module_add_array_ex(
        session->module, name.c_str(), FORGE_TYPE_PTR, kind, aggregate_name.c_str(),
        static_cast<std::uint32_t>(element_count), move_only != 0);
    if (index == SIZE_MAX) return 0;
    session->array_names.push_back(name);
    return static_cast<std::int64_t>(session->array_names.size());
}

extern "C" std::int64_t raz_compiler_forge_struct_add_field_i64(
    std::int64_t session_handle, std::int64_t struct_handle,
    std::int64_t name_handle, std::int64_t name_length,
    std::int64_t type, std::int64_t aggregate_kind,
    std::int64_t aggregate_name_handle, std::int64_t aggregate_name_length) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module || struct_handle <= 0) return 0;
    const std::string name = i64_ascii(name_handle, name_length);
    std::string aggregate_name = i64_ascii(aggregate_name_handle, aggregate_name_length);
    if (!aggregate_name.empty() && aggregate_name.front() == '@') aggregate_name.erase(aggregate_name.begin());
    return forge_struct_add_field(
        session->module, static_cast<std::size_t>(struct_handle - 1), name.empty() ? nullptr : name.c_str(),
        forge_type(type), forge_aggregate(aggregate_kind),
        aggregate_name.empty() ? nullptr : aggregate_name.c_str()) ? 1 : 0;
}

extern "C" std::int64_t raz_compiler_forge_session_add_global_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t type, std::int64_t aggregate_kind,
    std::int64_t aggregate_name_handle, std::int64_t aggregate_name_length,
    std::int64_t signature_handle, std::int64_t signature_length,
    std::int64_t is_constant, std::int64_t is_external,
    std::int64_t linkage, std::int64_t visibility,
    std::int64_t initializer_handle, std::int64_t initializer_length,
    std::int64_t element_count, std::int64_t alignment, std::int64_t zero_initialized) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module || element_count < 0 || alignment < 0) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    std::string aggregate_name = i64_ascii(aggregate_name_handle, aggregate_name_length);
    if (!aggregate_name.empty() && aggregate_name.front() == '@') aggregate_name.erase(aggregate_name.begin());
    std::string signature = i64_ascii(signature_handle, signature_length);
    if (!signature.empty() && signature.front() == '@') signature.erase(signature.begin());
    const std::string initializer = i64_ascii(initializer_handle, initializer_length);
    const std::size_t index = forge_module_add_global(
        session->module, name.c_str(), forge_type(type), forge_aggregate(aggregate_kind),
        aggregate_name.empty() ? nullptr : aggregate_name.c_str(),
        signature.empty() ? nullptr : signature.c_str(), is_constant != 0, is_external != 0,
        static_cast<forge_symbol_linkage_t>(linkage), static_cast<forge_symbol_visibility_t>(visibility),
        initializer.empty() ? nullptr : initializer.c_str(), static_cast<std::uint32_t>(element_count),
        static_cast<std::uint32_t>(alignment), zero_initialized != 0, nullptr, 0);
    if (index == SIZE_MAX) return 0;
    return static_cast<std::int64_t>(index + 1);
}

extern "C" std::int64_t raz_compiler_forge_session_add_global_ex_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t type, std::int64_t aggregate_kind,
    std::int64_t aggregate_name_handle, std::int64_t aggregate_name_length,
    std::int64_t signature_handle, std::int64_t signature_length,
    std::int64_t is_constant, std::int64_t is_external, std::int64_t is_thread_local,
    std::int64_t linkage, std::int64_t visibility,
    std::int64_t initializer_handle, std::int64_t initializer_length,
    std::int64_t element_count, std::int64_t alignment, std::int64_t zero_initialized) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module || element_count < 0 || alignment < 0) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    std::string aggregate_name = i64_ascii(aggregate_name_handle, aggregate_name_length);
    if (!aggregate_name.empty() && aggregate_name.front() == '@') aggregate_name.erase(aggregate_name.begin());
    std::string signature = i64_ascii(signature_handle, signature_length);
    if (!signature.empty() && signature.front() == '@') signature.erase(signature.begin());
    const std::string initializer = i64_ascii(initializer_handle, initializer_length);
    const std::size_t index = forge_module_add_global_ex(
        session->module, name.c_str(), forge_type(type), forge_aggregate(aggregate_kind),
        aggregate_name.empty() ? nullptr : aggregate_name.c_str(),
        signature.empty() ? nullptr : signature.c_str(), is_constant != 0, is_external != 0, is_thread_local != 0,
        static_cast<forge_symbol_linkage_t>(linkage), static_cast<forge_symbol_visibility_t>(visibility),
        initializer.empty() ? nullptr : initializer.c_str(), static_cast<std::uint32_t>(element_count),
        static_cast<std::uint32_t>(alignment), zero_initialized != 0, nullptr, 0);
    if (index == SIZE_MAX) return 0;
    return static_cast<std::int64_t>(index + 1);
}

extern "C" std::int64_t raz_compiler_forge_session_add_bytes_global_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t data_handle, std::int64_t data_length,
    std::int64_t is_constant, std::int64_t linkage, std::int64_t visibility,
    std::int64_t alignment) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module || data_length < 0 || alignment < 0) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    const auto* values = data_handle == 0 ? nullptr
        : reinterpret_cast<const std::int64_t*>(static_cast<std::uintptr_t>(data_handle));
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(data_length));
    for (std::int64_t index = 0; index < data_length; ++index)
        bytes.push_back(static_cast<std::uint8_t>(values[index] & 0xff));
    const std::size_t result = forge_module_add_global(
        session->module, name.c_str(), FORGE_TYPE_I8, FORGE_AGGREGATE_SCALAR, nullptr, nullptr,
        is_constant != 0, 0, static_cast<forge_symbol_linkage_t>(linkage),
        static_cast<forge_symbol_visibility_t>(visibility), nullptr,
        static_cast<std::uint32_t>(data_length), static_cast<std::uint32_t>(alignment), 0,
        bytes.empty() ? nullptr : bytes.data(), bytes.size());
    return result == SIZE_MAX ? 0 : static_cast<std::int64_t>(result + 1);
}

extern "C" std::int64_t raz_compiler_forge_session_add_bytes_global_ex_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t data_handle, std::int64_t data_length,
    std::int64_t is_constant, std::int64_t is_thread_local, std::int64_t linkage, std::int64_t visibility,
    std::int64_t alignment) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module || data_length < 0 || alignment < 0) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    const auto* values = data_handle == 0 ? nullptr
        : reinterpret_cast<const std::int64_t*>(static_cast<std::uintptr_t>(data_handle));
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(data_length));
    for (std::int64_t index = 0; index < data_length; ++index)
        bytes.push_back(static_cast<std::uint8_t>(values[index] & 0xff));
    const std::size_t result = forge_module_add_global_ex(
        session->module, name.c_str(), FORGE_TYPE_I8, FORGE_AGGREGATE_SCALAR, nullptr, nullptr,
        is_constant != 0, 0, is_thread_local != 0, static_cast<forge_symbol_linkage_t>(linkage),
        static_cast<forge_symbol_visibility_t>(visibility), nullptr,
        static_cast<std::uint32_t>(data_length), static_cast<std::uint32_t>(alignment), 0,
        bytes.empty() ? nullptr : bytes.data(), bytes.size());
    return result == SIZE_MAX ? 0 : static_cast<std::int64_t>(result + 1);
}

extern "C" std::int64_t raz_compiler_forge_session_add_function_i64(
    std::int64_t session_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t return_type, std::int64_t return_aggregate_kind,
    std::int64_t return_aggregate_name_handle, std::int64_t return_aggregate_name_length,
    std::int64_t return_owned, std::int64_t return_borrow_mode,
    std::int64_t return_borrow_parameter, std::int64_t is_external, std::int64_t is_signature) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    if (!session || !session->module) return 0;
    std::string name = i64_ascii(name_handle, name_length);
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    std::string aggregate_name = i64_ascii(return_aggregate_name_handle, return_aggregate_name_length);
    if (!aggregate_name.empty() && aggregate_name.front() == '@') aggregate_name.erase(aggregate_name.begin());
    auto* function = forge_function_create_ex(
        session->module, name.c_str(), forge_type(return_type), forge_aggregate(return_aggregate_kind),
        aggregate_name.empty() ? nullptr : aggregate_name.c_str(), return_owned != 0,
        forge_borrow(return_borrow_mode), static_cast<std::int32_t>(return_borrow_parameter),
        is_external != 0, is_signature != 0);
    if (!function) return 0;
    session->functions.push_back(function);
    return pointer_handle(function);
}

extern "C" std::int64_t raz_compiler_forge_function_add_parameter_i64(
    std::int64_t function_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t type, std::int64_t aggregate_kind,
    std::int64_t aggregate_name_handle, std::int64_t aggregate_name_length,
    std::int64_t owned, std::int64_t borrow_mode,
    std::int64_t signature_handle, std::int64_t signature_length) {
    auto* function = from_handle<forge_function_t>(function_handle);
    const std::string name = i64_ascii(name_handle, name_length);
    std::string aggregate_name = i64_ascii(aggregate_name_handle, aggregate_name_length);
    if (!aggregate_name.empty() && aggregate_name.front() == '@') aggregate_name.erase(aggregate_name.begin());
    const std::string signature = i64_ascii(signature_handle, signature_length);
    return forge_function_add_parameter(function, name.c_str(), forge_type(type), forge_aggregate(aggregate_kind),
                                        aggregate_name.empty() ? nullptr : aggregate_name.c_str(), owned != 0,
                                        forge_borrow(borrow_mode), signature.empty() ? nullptr : signature.c_str());
}

extern "C" std::int64_t raz_compiler_forge_function_set_abi_i64(
    std::int64_t function_handle, std::int64_t convention, std::int64_t variadic,
    std::int64_t linkage, std::int64_t visibility) {
    auto* function = from_handle<forge_function_t>(function_handle);
    return forge_function_set_abi(function, static_cast<forge_calling_convention_t>(convention), variadic != 0,
                                  static_cast<forge_symbol_linkage_t>(linkage),
                                  static_cast<forge_symbol_visibility_t>(visibility));
}

extern "C" std::int64_t raz_compiler_forge_function_set_target_feature_i64(
    std::int64_t function_handle, std::int64_t feature_handle, std::int64_t feature_length) {
    const std::string feature = i64_ascii(feature_handle, feature_length);
    return forge_function_set_target_feature(from_handle<forge_function_t>(function_handle), feature.c_str());
}

extern "C" std::int64_t raz_compiler_forge_session_add_block_i64(
    std::int64_t session_handle, std::int64_t function_handle,
    std::int64_t name_handle, std::int64_t name_length) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    auto* function = from_handle<forge_function_t>(function_handle);
    if (!session || !function) return 0;
    const std::string name = i64_ascii(name_handle, name_length);
    auto* block = forge_block_create(function, name.c_str());
    if (!block) return 0;
    session->blocks.push_back(block);
    return pointer_handle(block);
}

extern "C" std::int64_t raz_compiler_forge_block_add_parameter_i64(
    std::int64_t block_handle, std::int64_t name_handle, std::int64_t name_length,
    std::int64_t type, std::int64_t aggregate_kind,
    std::int64_t aggregate_name_handle, std::int64_t aggregate_name_length,
    std::int64_t owned, std::int64_t borrow_mode,
    std::int64_t signature_handle, std::int64_t signature_length) {
    const std::string name = i64_ascii(name_handle, name_length);
    std::string aggregate_name = i64_ascii(aggregate_name_handle, aggregate_name_length);
    if (!aggregate_name.empty() && aggregate_name.front() == '@') aggregate_name.erase(aggregate_name.begin());
    const std::string signature = i64_ascii(signature_handle, signature_length);
    return forge_block_add_parameter(from_handle<forge_block_t>(block_handle), name.c_str(), forge_type(type),
                                     forge_aggregate(aggregate_kind), aggregate_name.empty() ? nullptr : aggregate_name.c_str(),
                                     owned != 0, forge_borrow(borrow_mode), signature.empty() ? nullptr : signature.c_str());
}

extern "C" std::int64_t raz_compiler_forge_block_append_operation_i64(
    std::int64_t block_handle, std::int64_t result_handle, std::int64_t result_length,
    std::int64_t opcode_handle, std::int64_t opcode_length, std::int64_t type) {
    const std::string result = i64_ascii(result_handle, result_length);
    const std::string opcode = i64_ascii(opcode_handle, opcode_length);
    const auto index = forge_block_append_operation(from_handle<forge_block_t>(block_handle),
                                                    result.empty() ? nullptr : result.c_str(), opcode.c_str(), forge_type(type));
    return index == SIZE_MAX ? 0 : static_cast<std::int64_t>(index + 1);
}

extern "C" std::int64_t raz_compiler_forge_operation_add_operand_i64(
    std::int64_t block_handle, std::int64_t operation_handle,
    std::int64_t operand_handle, std::int64_t operand_length) {
    if (operation_handle <= 0) return 0;
    const std::string operand = i64_ascii(operand_handle, operand_length);
    return forge_operation_add_operand(from_handle<forge_block_t>(block_handle),
                                       static_cast<std::size_t>(operation_handle - 1), operand.c_str());
}

extern "C" std::int64_t raz_compiler_forge_operation_add_successor_i64(
    std::int64_t block_handle, std::int64_t operation_handle,
    std::int64_t successor_handle, std::int64_t successor_length) {
    if (operation_handle <= 0) return 0;
    const std::string successor = i64_ascii(successor_handle, successor_length);
    return forge_operation_add_successor(from_handle<forge_block_t>(block_handle),
                                         static_cast<std::size_t>(operation_handle - 1), successor.c_str());
}

extern "C" std::int64_t raz_compiler_forge_operation_add_successor_argument_i64(
    std::int64_t block_handle, std::int64_t operation_handle, std::int64_t successor_index,
    std::int64_t argument_handle, std::int64_t argument_length) {
    if (operation_handle <= 0 || successor_index < 0) return 0;
    const std::string argument = i64_ascii(argument_handle, argument_length);
    return forge_operation_add_successor_argument(from_handle<forge_block_t>(block_handle),
                                                  static_cast<std::size_t>(operation_handle - 1),
                                                  static_cast<std::size_t>(successor_index), argument.c_str());
}

extern "C" std::int64_t raz_compiler_forge_operation_set_alignment_i64(
    std::int64_t block_handle, std::int64_t operation_handle, std::int64_t alignment) {
    if (operation_handle <= 0 || alignment < 0) return 0;
    return forge_operation_set_alignment(from_handle<forge_block_t>(block_handle),
                                         static_cast<std::size_t>(operation_handle - 1),
                                         static_cast<std::uint32_t>(alignment));
}

extern "C" std::int64_t raz_compiler_forge_operation_add_attribute_i64(
    std::int64_t block_handle, std::int64_t operation_handle,
    std::int64_t name_handle, std::int64_t name_length,
    std::int64_t value_handle, std::int64_t value_length) {
    if (operation_handle <= 0) return 0;
    const std::string name = i64_ascii(name_handle, name_length);
    const std::string value = i64_ascii(value_handle, value_length);
    return forge_operation_add_attribute(from_handle<forge_block_t>(block_handle),
                                         static_cast<std::size_t>(operation_handle - 1), name.c_str(), value.c_str());
}

extern "C" std::int64_t raz_compiler_forge_session_emit_object_i64(
    std::int64_t session_handle, std::int64_t output_path_handle,
    std::int64_t output_path_length, std::int64_t optimization_level) {
    auto* session = from_handle<RazForgeSession>(session_handle);
    const std::string output_path = i64_ascii(output_path_handle, output_path_length);
    return session && session->module ? emit_module_object(session->module, output_path, optimization_level) : 0;
}
