// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

extern "C" {
std::int64_t raz_rt_env_get(const char* name, std::int64_t name_length, char* output, std::int64_t capacity) {
  const auto key = view_text(name, name_length);
  if (key.empty()) return -1;
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t value_length = 0;
  const errno_t status = _dupenv_s(&value, &value_length, key.c_str());
  if (status != 0 || value == nullptr) {
    std::free(value);
    return -1;
  }

  const std::string text(value, value_length > 0 ? value_length - 1 : 0);
  std::free(value);
  return copy_text(text, output, capacity);
#else
  const char* value = std::getenv(key.c_str());
  return value == nullptr ? -1 : copy_text(value, output, capacity);
#endif
}

std::int64_t raz_rt_env_set(const char* name, std::int64_t name_length,
                             const char* value, std::int64_t value_length) {
  const auto key = view_text(name, name_length);
  const auto text = view_text(value, value_length);
  if (key.empty()) return 0;
#if defined(_WIN32)
  return _putenv_s(key.c_str(), text.c_str()) == 0 ? 1 : 0;
#else
  return ::setenv(key.c_str(), text.c_str(), 1) == 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_env_remove(const char* name, std::int64_t name_length) {
  const auto key = view_text(name, name_length);
  if (key.empty()) return 0;
#if defined(_WIN32)
  return _putenv_s(key.c_str(), "") == 0 ? 1 : 0;
#else
  return ::unsetenv(key.c_str()) == 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_current_dir(char* output, std::int64_t capacity) {
  std::error_code error;
  const auto path = std::filesystem::current_path(error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return copy_text(path.string(), output, capacity);
}

std::int64_t raz_rt_create_dir_one(const char* path, std::int64_t length) {
  std::error_code error;
  const auto value = std::filesystem::path(view_text(path, length));
  if (value.empty()) { raz_set_last_error(EINVAL); return -1; }
  const bool created = std::filesystem::create_directory(value, error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return created ? 1 : 0;
}

std::int64_t raz_rt_remove_one(const char* path, std::int64_t length) {
  std::error_code error;
  const bool removed = std::filesystem::remove(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return removed ? 1 : 0;
}

std::int64_t raz_rt_path_exists(const char* path, std::int64_t length) {
  std::error_code error;
  const bool exists = std::filesystem::exists(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return exists ? 1 : 0;
}

std::int64_t raz_rt_file_size(const char* path, std::int64_t length) {
  std::error_code error;
  const auto size = std::filesystem::file_size(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(size);
}

std::int64_t raz_rt_path_is_file(const char* path, std::int64_t length) {
  std::error_code error;
  const bool value = std::filesystem::is_regular_file(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return value ? 1 : 0;
}

std::int64_t raz_rt_path_is_dir(const char* path, std::int64_t length) {
  std::error_code error;
  const bool value = std::filesystem::is_directory(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return value ? 1 : 0;
}

std::int64_t raz_rt_rename_path(const char* from, std::int64_t from_length,
                                 const char* to, std::int64_t to_length) {
  std::error_code error;
  std::filesystem::rename(std::filesystem::path(view_text(from, from_length)),
                          std::filesystem::path(view_text(to, to_length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return 1;
}

std::int64_t raz_rt_path_canonical(const char* path, std::int64_t length, char* output, std::int64_t capacity) {
  std::error_code error;
  const auto resolved = std::filesystem::canonical(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return copy_text(resolved.string(), output, capacity);
}

std::int64_t raz_rt_path_read_link(const char* path, std::int64_t length, char* output, std::int64_t capacity) {
  std::error_code error;
  const auto target = std::filesystem::read_symlink(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return copy_text(target.string(), output, capacity);
}

std::int64_t raz_rt_path_create_symlink(const char* target, std::int64_t target_length,
                                         const char* link, std::int64_t link_length,
                                         std::int64_t directory) {
  std::error_code error;
  const auto target_path = std::filesystem::path(view_text(target, target_length));
  const auto link_path = std::filesystem::path(view_text(link, link_length));
  if (target_path.empty() || link_path.empty()) { raz_set_last_error(EINVAL); return 0; }
  if (directory != 0) std::filesystem::create_directory_symlink(target_path, link_path, error);
  else std::filesystem::create_symlink(target_path, link_path, error);
  if (error) { raz_set_last_error(error.value()); return 0; }
  raz_clear_last_error();
  return 1;
}

std::int64_t raz_rt_temp_directory(char* output, std::int64_t capacity) {
  std::error_code error;
  const auto path = std::filesystem::temp_directory_path(error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return copy_text(path.string(), output, capacity);
}

std::int64_t raz_rt_create_temp_file(const char* prefix, std::int64_t prefix_length,
                                      char* output, std::int64_t capacity) {
  std::error_code error;
  const auto directory = std::filesystem::temp_directory_path(error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  auto stem = view_text(prefix, prefix_length);
  if (stem.empty()) stem = "raz";
  static std::atomic<std::uint64_t> sequence{0};
  std::random_device random;
  for (std::uint64_t attempt = 0; attempt < 128; ++attempt) {
    const auto nonce = (static_cast<std::uint64_t>(random()) << 32U) ^ static_cast<std::uint64_t>(random()) ^
                       sequence.fetch_add(1, std::memory_order_relaxed);
    const auto path = directory / (stem + "-" + std::to_string(nonce) + ".tmp");
#if defined(_WIN32)
    const auto wide = path.wstring();
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      const auto code = GetLastError();
      if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) continue;
      raz_set_last_error(static_cast<std::int64_t>(code));
      return -1;
    }
    CloseHandle(handle);
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
      if (errno == EEXIST) continue;
      raz_set_errno_error();
      return -1;
    }
    ::close(fd);
#endif
    raz_clear_last_error();
    return copy_text(path.string(), output, capacity);
  }
  raz_set_last_error(EEXIST);
  return -1;
}

std::int64_t raz_rt_path_permissions(const char* path, std::int64_t length) {
  std::error_code error;
  const auto status = std::filesystem::status(std::filesystem::path(view_text(path, length)), error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  const auto raw = static_cast<unsigned>(status.permissions());
  raz_clear_last_error();
  return static_cast<std::int64_t>(raw & 0777U);
}

std::int64_t raz_rt_path_set_permissions(const char* path, std::int64_t length, std::int64_t mode) {
  if (mode < 0 || mode > 0777) { raz_set_last_error(EINVAL); return 0; }
  std::error_code error;
  std::filesystem::permissions(std::filesystem::path(view_text(path, length)),
                               static_cast<std::filesystem::perms>(static_cast<unsigned>(mode)),
                               std::filesystem::perm_options::replace, error);
  if (error) { raz_set_last_error(error.value()); return 0; }
  raz_clear_last_error();
  return 1;
}

std::int64_t raz_rt_copy_file(const char* from, std::int64_t from_length,
                               const char* to, std::int64_t to_length, std::int64_t overwrite) {
  std::error_code error;
  const auto options = overwrite != 0 ? std::filesystem::copy_options::overwrite_existing
                                      : std::filesystem::copy_options::none;
  const bool copied = std::filesystem::copy_file(std::filesystem::path(view_text(from, from_length)),
                                                  std::filesystem::path(view_text(to, to_length)),
                                                  options, error);
  if (error) { raz_set_last_error(error.value()); return -1; }
  raz_clear_last_error();
  return copied ? 1 : 0;
}

void* raz_rt_file_open(const char* path, std::int64_t length, std::int64_t flags) {
  const auto value = view_text(path, length);
  if (value.empty()) { raz_set_last_error(EINVAL); return nullptr; }
  const bool read = (flags & 1) != 0;
  const bool write = (flags & 2) != 0;
  const bool append = (flags & 4) != 0;
  const bool create = (flags & 8) != 0;
  const bool truncate = (flags & 16) != 0;
  const bool exclusive = (flags & 32) != 0;
  if (!read && !write) { raz_set_last_error(EINVAL); return nullptr; }
  if (exclusive && !create) { raz_set_last_error(EINVAL); return nullptr; }

  const char* mode = nullptr;
  if (append) mode = read ? "a+b" : "ab";
  else if (truncate) mode = read ? "w+b" : "wb";
  else if (read && write) mode = "r+b";
  else if (write) mode = "r+b";
  else mode = "rb";

  // fopen cannot express race-free exclusive creation or create-without-truncate.
  // Use the native descriptor boundary for those modes, then preserve the FILE*
  // runtime ABI above it.
  if (exclusive || (create && !truncate && !append)) {
#if defined(_WIN32)
    int open_flags = _O_BINARY | _O_CREAT;
    if (exclusive) open_flags |= _O_EXCL;
    if (read && write) open_flags |= _O_RDWR;
    else if (write) open_flags |= _O_WRONLY;
    else open_flags |= _O_RDONLY;
    const int descriptor = _open(value.c_str(), open_flags, _S_IREAD | _S_IWRITE);
    if (descriptor < 0) { raz_set_errno_error(); return nullptr; }
    std::FILE* file = _fdopen(descriptor, read && write ? "r+b" : (write ? "wb" : "rb"));
    if (file == nullptr) { _close(descriptor); raz_set_errno_error(); return nullptr; }
#else
    int open_flags = O_CREAT;
    if (exclusive) open_flags |= O_EXCL;
    if (read && write) open_flags |= O_RDWR;
    else if (write) open_flags |= O_WRONLY;
    else open_flags |= O_RDONLY;
    const int descriptor = ::open(value.c_str(), open_flags, 0666);
    if (descriptor < 0) { raz_set_errno_error(); return nullptr; }
    std::FILE* file = ::fdopen(descriptor, read && write ? "r+b" : (write ? "wb" : "rb"));
    if (file == nullptr) { ::close(descriptor); raz_set_errno_error(); return nullptr; }
#endif
    raz_clear_last_error();
    return file;
  }

  std::FILE* file = std::fopen(value.c_str(), mode);
  if (file == nullptr && create && read && write) file = std::fopen(value.c_str(), "w+b");
  if (file == nullptr) raz_set_errno_error();
  else raz_clear_last_error();
  return file;
}

std::int64_t raz_rt_file_read(void* handle, void* output, std::int64_t capacity) {
  if (handle == nullptr || output == nullptr || capacity < 0) { raz_set_last_error(EINVAL); return -1; }
  if (capacity == 0) { raz_clear_last_error(); return 0; }
  auto* file = static_cast<std::FILE*>(handle);
  const auto count = std::fread(output, 1, static_cast<std::size_t>(capacity), file);
  if (count == 0 && std::ferror(file) != 0) { raz_set_errno_error(); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(count);
}

std::int64_t raz_rt_file_write(void* handle, const void* data, std::int64_t size) {
  if (handle == nullptr || data == nullptr || size < 0) { raz_set_last_error(EINVAL); return -1; }
  if (size == 0) { raz_clear_last_error(); return 0; }
  auto* file = static_cast<std::FILE*>(handle);
  const auto count = std::fwrite(data, 1, static_cast<std::size_t>(size), file);
  if (count == 0 && std::ferror(file) != 0) { raz_set_errno_error(); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(count);
}

std::int64_t raz_rt_file_seek(void* handle, std::int64_t offset, std::int64_t origin) {
  if (handle == nullptr || origin < 0 || origin > 2) { raz_set_last_error(EINVAL); return 0; }
  const int base = origin == 0 ? SEEK_SET : (origin == 1 ? SEEK_CUR : SEEK_END);
#if defined(_WIN32)
  const bool ok = _fseeki64(static_cast<std::FILE*>(handle), offset, base) == 0;
#else
  const bool ok = ::fseeko(static_cast<std::FILE*>(handle), static_cast<off_t>(offset), base) == 0;
#endif
  if (!ok) { raz_set_errno_error(); return 0; }
  raz_clear_last_error();
  return 1;
}

std::int64_t raz_rt_file_tell(void* handle) {
  if (handle == nullptr) { raz_set_last_error(EINVAL); return -1; }
#if defined(_WIN32)
  const auto position = static_cast<std::int64_t>(_ftelli64(static_cast<std::FILE*>(handle)));
#else
  const auto position = static_cast<std::int64_t>(::ftello(static_cast<std::FILE*>(handle)));
#endif
  if (position < 0) { raz_set_errno_error(); return -1; }
  raz_clear_last_error();
  return position;
}

std::int64_t raz_rt_file_flush(void* handle) {
  if (handle == nullptr) { raz_set_last_error(EINVAL); return 0; }
  if (std::fflush(static_cast<std::FILE*>(handle)) != 0) { raz_set_errno_error(); return 0; }
  raz_clear_last_error();
  return 1;
}

std::int64_t raz_rt_file_eof(void* handle) {
  return handle != nullptr && std::feof(static_cast<std::FILE*>(handle)) != 0 ? 1 : 0;
}

void raz_rt_file_close(void* handle) {
  if (handle != nullptr) std::fclose(static_cast<std::FILE*>(handle));
}

std::int64_t raz_rt_read_ascii_i64(const std::int64_t* path_codes, std::int64_t path_length,
                                        std::int64_t* data_codes, std::int64_t capacity) {
  if (path_codes == nullptr || data_codes == nullptr || path_length < 0 || capacity < 0) return -1;
  std::string path; path.reserve(static_cast<std::size_t>(path_length));
  for (std::int64_t i = 0; i < path_length; ++i) path.push_back(static_cast<char>(path_codes[i] & 0xff));
  std::ifstream input(path, std::ios::binary);
  if (!input) return -1;
  std::int64_t count = 0;
  char byte = 0;
  while (count < capacity && input.get(byte)) {
    data_codes[count] = static_cast<unsigned char>(byte);
    ++count;
  }
  return count;
}

std::int64_t raz_rt_write_ascii_i64(const std::int64_t* path_codes, std::int64_t path_length,
                                         const std::int64_t* data_codes, std::int64_t size) {
  if (path_codes == nullptr || data_codes == nullptr || path_length < 0 || size < 0) return -1;
  std::string path; path.reserve(static_cast<std::size_t>(path_length));
  for (std::int64_t i = 0; i < path_length; ++i) path.push_back(static_cast<char>(path_codes[i] & 0xff));
  std::vector<char> bytes; bytes.reserve(static_cast<std::size_t>(size));
  for (std::int64_t i = 0; i < size; ++i) bytes.push_back(static_cast<char>(data_codes[i] & 0xff));
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return -1;
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return output ? size : -1;
}

std::int64_t raz_rt_fs_metadata(const char* path, std::int64_t length, std::int64_t* output, std::int64_t capacity) {
  if (path == nullptr || length < 0 || output == nullptr || capacity < 6) {
    raz_set_last_error(EINVAL);
    return 0;
  }
  std::error_code error;
  const auto fs_path = std::filesystem::path(view_text(path, length));
  const auto status = std::filesystem::symlink_status(fs_path, error);
  if (error) { raz_set_last_error(error.value()); return 0; }
  std::int64_t kind = 0;
  if (std::filesystem::is_regular_file(status)) kind = 1;
  else if (std::filesystem::is_directory(status)) kind = 2;
  else if (std::filesystem::is_symlink(status)) kind = 3;
  else if (std::filesystem::exists(status)) kind = 4;
  std::int64_t size = 0;
  if (kind == 1) {
    const auto raw_size = std::filesystem::file_size(fs_path, error);
    if (error) { raz_set_last_error(error.value()); return 0; }
    size = raw_size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(raw_size);
  }
  const auto write_bits = std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                          std::filesystem::perms::others_write;
  const auto readonly = (status.permissions() & write_bits) == std::filesystem::perms::none ? 1 : 0;
  std::int64_t modified_millis = -1;
  std::int64_t modified_ticks = -1;
  const auto modified = std::filesystem::last_write_time(fs_path, error);
  if (!error) {
    auto raw_ticks = modified.time_since_epoch().count();
    modified_ticks = raw_ticks > static_cast<decltype(raw_ticks)>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : raw_ticks < static_cast<decltype(raw_ticks)>(std::numeric_limits<std::int64_t>::min())
            ? std::numeric_limits<std::int64_t>::min()
            : static_cast<std::int64_t>(raw_ticks);
    // Convert file_clock into system_clock without assuming identical epochs.
    const auto system_now = std::chrono::system_clock::now();
    const auto file_now = decltype(modified)::clock::now();
    const auto system_time = system_now + std::chrono::duration_cast<std::chrono::system_clock::duration>(modified - file_now);
    modified_millis = std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch()).count();
  } else {
    error.clear();
  }
  output[0] = kind;
  output[1] = size;
  output[2] = modified_millis;
  output[3] = readonly;
  output[4] = std::filesystem::is_symlink(status) ? 1 : 0;
  output[5] = std::filesystem::exists(status) ? 1 : 0;
  if (capacity >= 7) output[6] = modified_ticks;
  raz_clear_last_error();
  return 1;
}

void* raz_rt_dir_open(const char* path, std::int64_t length) {
  if (path == nullptr || length < 0) { raz_set_last_error(EINVAL); return nullptr; }
  auto* handle = new (std::nothrow) RazDirectoryIterator();
  if (handle == nullptr) { raz_set_last_error(ENOMEM); return nullptr; }
  handle->current = std::filesystem::directory_iterator(std::filesystem::path(view_text(path, length)), handle->error);
  if (handle->error) {
    raz_set_last_error(handle->error.value());
    delete handle;
    return nullptr;
  }

  raz_clear_last_error();
  return handle;
}

// Returns required UTF-8 byte length, -1 at end, -2 on error. Metadata layout:
// kind, size, modified_millis, readonly, is_symlink, exists.
std::int64_t raz_rt_dir_next(void* raw_handle, char* output, std::int64_t capacity,
                              std::int64_t* metadata, std::int64_t metadata_capacity) {
  if (raw_handle == nullptr || metadata == nullptr || metadata_capacity < 6) {
    raz_set_last_error(EINVAL);
    return -2;
  }
  auto* handle = static_cast<RazDirectoryIterator*>(raw_handle);
  if (handle->current == handle->end) { raz_clear_last_error(); return -1; }
  const auto entry = *handle->current;
  std::error_code error;
  const auto name = entry.path().filename().string();
  const auto status = entry.symlink_status(error);
  if (error) { raz_set_last_error(error.value()); return -2; }
  std::int64_t kind = 0;
  if (std::filesystem::is_regular_file(status)) kind = 1;
  else if (std::filesystem::is_directory(status)) kind = 2;
  else if (std::filesystem::is_symlink(status)) kind = 3;
  else if (std::filesystem::exists(status)) kind = 4;
  std::int64_t size = 0;
  if (kind == 1) {
    const auto raw_size = entry.file_size(error);
    if (!error) size = raw_size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max() : static_cast<std::int64_t>(raw_size);
    error.clear();
  }
  std::int64_t modified_millis = -1;
  const auto modified = entry.last_write_time(error);
  if (!error) {
    const auto system_now = std::chrono::system_clock::now();
    const auto file_now = decltype(modified)::clock::now();
    const auto system_time = system_now + std::chrono::duration_cast<std::chrono::system_clock::duration>(modified - file_now);
    modified_millis = std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch()).count();
  }
  error.clear();
  const auto write_bits = std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                          std::filesystem::perms::others_write;
  metadata[0] = kind;
  metadata[1] = size;
  metadata[2] = modified_millis;
  metadata[3] = (status.permissions() & write_bits) == std::filesystem::perms::none ? 1 : 0;
  metadata[4] = std::filesystem::is_symlink(status) ? 1 : 0;
  metadata[5] = std::filesystem::exists(status) ? 1 : 0;
  const auto required = copy_text(name, output, capacity);
  // A sizing call must not consume the entry. Raz can reserve exactly once and
  // then fetch the same entry without a fixed-size filename buffer.
  if (output == nullptr || capacity <= required) { raz_clear_last_error(); return required; }
  handle->current.increment(handle->error);
  if (handle->error) { raz_set_last_error(handle->error.value()); return -2; }
  raz_clear_last_error();
  return required;
}

void raz_rt_dir_close(void* raw_handle) {
  delete static_cast<RazDirectoryIterator*>(raw_handle);
}

std::int64_t raz_rt_process_run_argv(const char* program, std::int64_t program_length,
                                      const char* blob, std::int64_t blob_length,
                                      std::int64_t argument_count) {
  if (program == nullptr || program_length <= 0 || blob_length < 0 || argument_count < 0 ||
      (blob_length > 0 && blob == nullptr)) {
    raz_set_last_error(EINVAL);
    return -1;
  }
  const auto executable = view_text(program, program_length);
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argument_count) + 1);
  arguments.push_back(executable);
  std::int64_t cursor = 0;
  for (std::int64_t index = 0; index < argument_count; ++index) {
    if (cursor > blob_length) { raz_set_last_error(EINVAL); return -1; }
    const auto start = cursor;
    while (cursor < blob_length && blob[cursor] != '\0') ++cursor;
    if (cursor >= blob_length) { raz_set_last_error(EINVAL); return -1; }
    arguments.emplace_back(blob + start, static_cast<std::size_t>(cursor - start));
    ++cursor;
  }
#if defined(_WIN32)
  auto quote_windows_argument = [](const std::string& value) {
    if (value.empty()) return std::string("\"\"");
    const bool needs_quotes = value.find_first_of(" \t\"") != std::string::npos;
    if (!needs_quotes) return value;
    std::string out; out.push_back('"');
    std::size_t slashes = 0;
    for (const char ch : value) {
      if (ch == '\\') { ++slashes; continue; }
      if (ch == '"') {
        out.append(slashes * 2 + 1, '\\'); out.push_back('"'); slashes = 0; continue;
      }
      out.append(slashes, '\\'); slashes = 0; out.push_back(ch);
    }
    out.append(slashes * 2, '\\'); out.push_back('"');
    return out;
  };
  std::string command_line;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (i != 0) command_line.push_back(' ');
    command_line += quote_windows_argument(arguments[i]);
  }

  std::vector<char> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back('\0');
  STARTUPINFOA startup{}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    raz_set_last_error(static_cast<std::int64_t>(GetLastError())); return -1;
  }

  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 1;
  if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = 1;
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  raz_clear_last_error();
  return static_cast<std::int64_t>(exit_code);
#else
  const pid_t child = ::fork();
  if (child < 0) { raz_set_errno_error(); return -1; }
  if (child == 0) {
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    ::execvp(executable.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno == EINTR) continue;
    raz_set_errno_error(); return -1;
  }

  raz_clear_last_error();
  if (WIFEXITED(status)) return static_cast<std::int64_t>(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return 128 + static_cast<std::int64_t>(WTERMSIG(status));
  return -1;
#endif
}

std::int64_t raz_rt_process_run_argv_cwd(const char* program, std::int64_t program_length,
                                          const char* blob, std::int64_t blob_length,
                                          std::int64_t argument_count,
                                          const char* working_directory,
                                          std::int64_t working_directory_length) {
  if (program == nullptr || program_length <= 0 || blob_length < 0 || argument_count < 0 ||
      working_directory == nullptr || working_directory_length <= 0 ||
      (blob_length > 0 && blob == nullptr)) {
    raz_set_last_error(EINVAL);
    return -1;
  }
  const auto executable = view_text(program, program_length);
  const auto cwd = view_text(working_directory, working_directory_length);
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argument_count) + 1);
  arguments.push_back(executable);
  std::int64_t cursor = 0;
  for (std::int64_t index = 0; index < argument_count; ++index) {
    if (cursor > blob_length) { raz_set_last_error(EINVAL); return -1; }
    const auto start = cursor;
    while (cursor < blob_length && blob[cursor] != '\0') ++cursor;
    if (cursor >= blob_length) { raz_set_last_error(EINVAL); return -1; }
    arguments.emplace_back(blob + start, static_cast<std::size_t>(cursor - start));
    ++cursor;
  }
#if defined(_WIN32)
  auto quote_windows_argument = [](const std::string& value) {
    if (value.empty()) return std::string("\"\"");
    const bool needs_quotes = value.find_first_of(" \t\"") != std::string::npos;
    if (!needs_quotes) return value;
    std::string out; out.push_back('"');
    std::size_t slashes = 0;
    for (const char ch : value) {
      if (ch == '\\') { ++slashes; continue; }
      if (ch == '"') {
        out.append(slashes * 2 + 1, '\\'); out.push_back('"'); slashes = 0; continue;
      }
      out.append(slashes, '\\'); slashes = 0; out.push_back(ch);
    }
    out.append(slashes * 2, '\\'); out.push_back('"');
    return out;
  };
  std::string command_line;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (i != 0) command_line.push_back(' ');
    command_line += quote_windows_argument(arguments[i]);
  }

  std::vector<char> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back('\0');
  STARTUPINFOA startup{}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      cwd.c_str(), &startup, &process)) {
    raz_set_last_error(static_cast<std::int64_t>(GetLastError())); return -1;
  }

  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 1;
  if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = 1;
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  raz_clear_last_error();
  return static_cast<std::int64_t>(exit_code);
#else
  const pid_t child = ::fork();
  if (child < 0) { raz_set_errno_error(); return -1; }
  if (child == 0) {
    if (::chdir(cwd.c_str()) != 0) _exit(126);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    ::execvp(executable.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno == EINTR) continue;
    raz_set_errno_error(); return -1;
  }

  raz_clear_last_error();
  if (WIFEXITED(status)) return static_cast<std::int64_t>(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return 128 + static_cast<std::int64_t>(WTERMSIG(status));
  return -1;
#endif
}

std::int64_t raz_rt_process_argc() {
  return static_cast<std::int64_t>(process_arguments().size());
}

std::int64_t raz_rt_process_arg(std::int64_t index, char* output, std::int64_t capacity) {
  const auto& arguments = process_arguments();
  if (index < 0 || static_cast<std::size_t>(index) >= arguments.size()) return -1;
  return copy_text(arguments[static_cast<std::size_t>(index)], output, capacity);
}

static std::FILE* raz_stdio_stream(std::int64_t stream) {
  if (stream == 0) return stdin;
  if (stream == 1) return stdout;
  if (stream == 2) return stderr;
  return nullptr;
}

std::int64_t raz_rt_stdio_read(std::int64_t stream, void* output, std::int64_t capacity) {
  auto* file = raz_stdio_stream(stream);
  if (file == nullptr || stream != 0 || output == nullptr || capacity < 0) return -1;
  if (capacity == 0) return 0;
  const auto count = std::fread(output, 1, static_cast<std::size_t>(capacity), file);
  if (count == 0 && std::ferror(file) != 0) return -1;
  return static_cast<std::int64_t>(count);
}

std::int64_t raz_rt_stdio_write(std::int64_t stream, const void* data, std::int64_t size) {
  auto* file = raz_stdio_stream(stream);
  if (file == nullptr || stream == 0 || data == nullptr || size < 0) return -1;
  if (size == 0) return 0;
  const auto count = std::fwrite(data, 1, static_cast<std::size_t>(size), file);
  if (count == 0 && std::ferror(file) != 0) return -1;
  return static_cast<std::int64_t>(count);
}

std::int64_t raz_rt_stdio_flush(std::int64_t stream) {
  auto* file = raz_stdio_stream(stream);
  return file != nullptr && std::fflush(file) == 0 ? 1 : 0;
}

std::int64_t raz_rt_stdio_set_binary(std::int64_t stream) {
  auto* file = raz_stdio_stream(stream);
  if (file == nullptr) return 0;
#if defined(_WIN32)
  // The Microsoft CRT opens the standard streams in text mode by default.
  // Text mode rewrites every '\n' written through fwrite() to "\r\n".
  // Protocols such as LSP already serialize their required CRLF framing, so
  // leaving stdout in text mode turns "\r\n" into "\r\r\n" and corrupts
  // the Content-Length header.  Binary mode is process-local and exactly
  // preserves the byte stream supplied by the Raz protocol implementation.
  const int descriptor = _fileno(file);
  if (descriptor < 0) return 0;
  return _setmode(descriptor, _O_BINARY) == -1 ? 0 : 1;
#else
  // POSIX stdio has no text-mode newline translation.
  (void)file;
  return 1;
#endif
}

std::int64_t raz_rt_stdio_is_terminal(std::int64_t stream) {
  auto* file = raz_stdio_stream(stream);
  if (file == nullptr) return 0;
#if defined(_WIN32)
  return _isatty(_fileno(file)) != 0 ? 1 : 0;
#else
  return ::isatty(fileno(file)) != 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_process_run(const char* command, std::int64_t length) {
  const auto value = view_text(command, length);
  return value.empty() ? -1 : static_cast<std::int64_t>(std::system(value.c_str()));
}


}
