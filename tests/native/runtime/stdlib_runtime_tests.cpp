// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern "C" {
std::int64_t raz_rt_load_u8(std::uintptr_t address);
std::int64_t raz_rt_store_u8(std::uintptr_t address, std::int64_t value);
std::int64_t raz_rt_bytes_equal(std::uintptr_t left, std::uintptr_t right, std::int64_t size);
std::int64_t raz_rt_bytes_compare(std::uintptr_t left, std::uintptr_t right, std::int64_t size);
std::int64_t raz_rt_env_get(const char* name, std::int64_t name_length, char* output, std::int64_t capacity);
std::int64_t raz_rt_env_set(const char* name, std::int64_t name_length, const char* value, std::int64_t value_length);
std::int64_t raz_rt_env_remove(const char* name, std::int64_t name_length);
std::int64_t raz_rt_path_is_file(const char* path, std::int64_t length);
std::int64_t raz_rt_path_is_dir(const char* path, std::int64_t length);
std::int64_t raz_rt_rename_path(const char* from, std::int64_t from_length, const char* to, std::int64_t to_length);
std::int64_t raz_rt_copy_file(const char* from, std::int64_t from_length, const char* to, std::int64_t to_length, std::int64_t overwrite);
void* raz_rt_file_open(const char* path, std::int64_t length, std::int64_t flags);
std::int64_t raz_rt_file_read(void* handle, void* output, std::int64_t capacity);
std::int64_t raz_rt_file_write(void* handle, const void* data, std::int64_t size);
std::int64_t raz_rt_file_seek(void* handle, std::int64_t offset, std::int64_t origin);
std::int64_t raz_rt_file_tell(void* handle);
std::int64_t raz_rt_file_flush(void* handle);
std::int64_t raz_rt_file_eof(void* handle);
void raz_rt_file_close(void* handle);
std::int64_t raz_rt_fs_metadata(const char* path, std::int64_t length, std::int64_t* output, std::int64_t capacity);
void* raz_rt_dir_open(const char* path, std::int64_t length);
std::int64_t raz_rt_dir_next(void* handle, char* output, std::int64_t capacity, std::int64_t* metadata, std::int64_t metadata_capacity);
void raz_rt_dir_close(void* handle);
std::int64_t raz_rt_create_dir_one(const char* path, std::int64_t length);
std::int64_t raz_rt_remove_one(const char* path, std::int64_t length);
std::int64_t raz_rt_process_run_argv(const char* program, std::int64_t program_length, const char* blob, std::int64_t blob_length, std::int64_t argument_count);
std::int64_t raz_rt_process_argc();
std::int64_t raz_rt_process_arg(std::int64_t index, char* output, std::int64_t capacity);
std::int64_t raz_rt_stdio_is_terminal(std::int64_t stream);
std::int64_t raz_rt_udp_bind(std::int64_t port);
std::int64_t raz_rt_socket_local_port(std::int64_t socket_value);
std::int64_t raz_rt_socket_set_timeout_millis(std::int64_t socket_value, std::int64_t receive_millis, std::int64_t send_millis);
std::int64_t raz_rt_udp_send_to(std::int64_t socket_value, const char* host, std::int64_t host_length, std::int64_t port, const void* data, std::int64_t size);
std::int64_t raz_rt_udp_receive_from(std::int64_t socket_value, void* data, std::int64_t capacity, char* address_output, std::int64_t address_capacity, std::int64_t* port_output);
void raz_rt_socket_close(std::int64_t socket_value);
}

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "stdlib runtime failure: " << message << '\n';
    return condition;
}
}

int main() {
    bool ok = true;

    char bytes[4]{1, 2, 3, 4};
    char same[4]{1, 2, 3, 4};
    ok &= expect(raz_rt_load_u8(reinterpret_cast<std::uintptr_t>(bytes + 2)) == 3, "load_u8");
    ok &= expect(raz_rt_store_u8(reinterpret_cast<std::uintptr_t>(bytes + 1), 9) == 1 && bytes[1] == 9, "store_u8");
    same[1] = 9;
    ok &= expect(raz_rt_bytes_equal(reinterpret_cast<std::uintptr_t>(bytes), reinterpret_cast<std::uintptr_t>(same), 4) == 1, "bytes_equal");
    same[3] = 5;
    ok &= expect(raz_rt_bytes_compare(reinterpret_cast<std::uintptr_t>(bytes), reinterpret_cast<std::uintptr_t>(same), 4) < 0, "bytes_compare");

    const std::string env_name = "RAZ_STDLIB_RUNTIME_TEST";
    const std::string env_value = "stdlib-runtime";
    char env_buffer[64]{};
    ok &= expect(raz_rt_env_set(env_name.data(), env_name.size(), env_value.data(), env_value.size()) == 1, "env_set");
    ok &= expect(raz_rt_env_get(env_name.data(), env_name.size(), env_buffer, sizeof(env_buffer)) == static_cast<std::int64_t>(env_value.size()), "env_get length");
    ok &= expect(std::string(env_buffer) == env_value, "env_get value");
    ok &= expect(raz_rt_env_remove(env_name.data(), env_name.size()) == 1, "env_remove");

    const auto base = std::filesystem::temp_directory_path() / ("raz-stdlib-runtime-" + std::to_string(raz_rt_process_argc()));
    const auto first_path = (base.string() + "-first.bin");
    const auto copy_path = (base.string() + "-copy.bin");
    const auto moved_path = (base.string() + "-moved.bin");
    std::error_code ignored;
    std::filesystem::remove(first_path, ignored);
    std::filesystem::remove(copy_path, ignored);
    std::filesystem::remove(moved_path, ignored);

    void* file = raz_rt_file_open(first_path.data(), first_path.size(), 1 | 2 | 8 | 16);
    ok &= expect(file != nullptr, "file_open");
    const char payload[] = "abcdef";
    if (file != nullptr) {
        ok &= expect(raz_rt_file_write(file, payload, 6) == 6, "file_write");
        ok &= expect(raz_rt_file_flush(file) == 1, "file_flush");
        ok &= expect(raz_rt_file_tell(file) == 6, "file_tell after write");
        ok &= expect(raz_rt_file_seek(file, 0, 0) == 1, "file_seek start");
        char readback[8]{};
        ok &= expect(raz_rt_file_read(file, readback, 6) == 6, "file_read");
        ok &= expect(std::memcmp(readback, payload, 6) == 0, "file_read content");
        ok &= expect(raz_rt_file_read(file, readback, 1) == 0, "file_read eof");
        ok &= expect(raz_rt_file_eof(file) == 1, "file_eof");
        raz_rt_file_close(file);
    }
    ok &= expect(raz_rt_path_is_file(first_path.data(), first_path.size()) == 1, "path_is_file");
    const auto temp_dir = std::filesystem::temp_directory_path().string();
    ok &= expect(raz_rt_path_is_dir(temp_dir.data(), temp_dir.size()) == 1, "path_is_dir");
    ok &= expect(raz_rt_copy_file(first_path.data(), first_path.size(), copy_path.data(), copy_path.size(), 0) == 1, "copy_file");
    ok &= expect(raz_rt_rename_path(copy_path.data(), copy_path.size(), moved_path.data(), moved_path.size()) == 1, "rename_path");
    ok &= expect(raz_rt_path_is_file(moved_path.data(), moved_path.size()) == 1, "renamed file exists");

    std::int64_t metadata[6]{};
    ok &= expect(raz_rt_fs_metadata(first_path.data(), first_path.size(), metadata, 6) == 1, "fs metadata");
    ok &= expect(metadata[0] == 1 && metadata[1] == 6 && metadata[5] == 1, "fs metadata contents");

    const auto dir_path = base.string() + "-dir";
    std::filesystem::remove_all(dir_path, ignored);
    ok &= expect(raz_rt_create_dir_one(dir_path.data(), dir_path.size()) == 1, "create_dir_one");
    const auto nested_file = std::filesystem::path(dir_path) / "entry.txt";
    { std::ofstream out(nested_file, std::ios::binary); out << "xyz"; }
    void* directory = raz_rt_dir_open(dir_path.data(), dir_path.size());
    ok &= expect(directory != nullptr, "dir_open");
    if (directory != nullptr) {
        std::int64_t entry_meta[6]{};
        const auto required = raz_rt_dir_next(directory, nullptr, 0, entry_meta, 6);
        ok &= expect(required == 9, "dir_next sizing");
        char entry_name[32]{};
        ok &= expect(raz_rt_dir_next(directory, entry_name, sizeof(entry_name), entry_meta, 6) == 9, "dir_next fetch");
        ok &= expect(std::string(entry_name) == "entry.txt" && entry_meta[0] == 1 && entry_meta[1] == 3, "dir_next contents");
        ok &= expect(raz_rt_dir_next(directory, entry_name, sizeof(entry_name), entry_meta, 6) == -1, "dir_next end");
        raz_rt_dir_close(directory);
    }

    std::filesystem::remove(nested_file, ignored);
    ok &= expect(raz_rt_remove_one(dir_path.data(), dir_path.size()) == 1, "remove_one");

#if defined(_WIN32)
    const std::string command_program = "cmd.exe";
    const std::string command_blob = std::string("/C\0exit 7\0", 10);
#else
    const std::string command_program = "/bin/sh";
    const std::string command_blob = std::string("-c\0exit 7\0", 10);
#endif
    ok &= expect(raz_rt_process_run_argv(command_program.data(), command_program.size(), command_blob.data(), command_blob.size(), 2) == 7, "process_run_argv");

    ok &= expect(raz_rt_process_argc() >= 1, "process_argc");
    char arg0[1024]{};
    ok &= expect(raz_rt_process_arg(0, arg0, sizeof(arg0)) > 0, "process_arg");
    ok &= expect(raz_rt_stdio_is_terminal(0) == 0 || raz_rt_stdio_is_terminal(0) == 1, "stdio_is_terminal");

    const std::int64_t receiver = raz_rt_udp_bind(0);
    const std::int64_t sender = raz_rt_udp_bind(0);
    if (receiver >= 0 && sender >= 0) {
        const std::int64_t port = raz_rt_socket_local_port(receiver);
        raz_rt_socket_set_timeout_millis(receiver, 1000, 1000);
        const char datagram[] = "udp42";
        ok &= expect(port > 0, "udp local port");
        ok &= expect(raz_rt_udp_send_to(sender, "127.0.0.1", 9, port, datagram, 5) == 5, "udp send_to");
        char received[16]{};
        char address[64]{};
        std::int64_t source_port = -1;
        ok &= expect(raz_rt_udp_receive_from(receiver, received, sizeof(received), address, sizeof(address), &source_port) == 5, "udp receive_from");
        ok &= expect(std::memcmp(received, datagram, 5) == 0, "udp payload");
        ok &= expect(source_port > 0, "udp source port");
        ok &= expect(std::string(address) == "127.0.0.1", "udp source address");
    } else {
        ok &= expect(false, "udp bind");
    }

    if (receiver >= 0) raz_rt_socket_close(receiver);
    if (sender >= 0) raz_rt_socket_close(sender);

    std::filesystem::remove(first_path, ignored);
    std::filesystem::remove(moved_path, ignored);
    return ok ? 0 : 1;
}
