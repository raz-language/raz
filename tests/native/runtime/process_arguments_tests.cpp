// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// These are permanent native process boundaries owned by Raz::runtime.  The
// compiler's raz_compiler_rt_* adapters deliberately live in Raz source and
// must never be duplicated here merely to make a native unit test link.
extern "C" {
std::int64_t raz_rt_process_argc();
std::int64_t raz_rt_process_arg(std::int64_t index, char* output, std::int64_t capacity);
std::int64_t raz_rt_process_run_argv(const char* program, std::int64_t program_length,
                                     const char* blob, std::int64_t blob_length,
                                     std::int64_t argument_count);
}

namespace {

bool expect_runtime_arguments(int argc, char** argv) {
  if (raz_rt_process_argc() != argc) {
    std::cerr << "runtime argc does not match process argc\n";
    return false;
  }

  for (int index = 0; index < argc; ++index) {
    const std::string expected = argv[index] == nullptr ? "" : argv[index];

    // A null output is a sizing query.  The native boundary returns the full
    // byte count so callers can allocate exactly once instead of guessing at a
    // fixed maximum argument size.
    const auto required = raz_rt_process_arg(index, nullptr, 0);
    if (required != static_cast<std::int64_t>(expected.size())) {
      std::cerr << "argument sizing mismatch at index " << index << "\n";
      return false;
    }

    std::vector<char> storage(static_cast<std::size_t>(required) + 1U, '\0');
    const auto copied = raz_rt_process_arg(index, storage.data(), static_cast<std::int64_t>(storage.size()));
    if (copied != required) {
      std::cerr << "argument copy length mismatch at index " << index << "\n";
      return false;
    }
    if (std::string_view(storage.data(), static_cast<std::size_t>(required)) != expected) {
      std::cerr << "argument mismatch at index " << index << "\n";
      return false;
    }
  }

  if (raz_rt_process_arg(argc, nullptr, 0) != -1 || raz_rt_process_arg(-1, nullptr, 0) != -1) {
    std::cerr << "out-of-range process argument lookup was not rejected\n";
    return false;
  }
  return true;
}

std::string pack_arguments(const std::vector<std::string>& arguments) {
  std::string blob;
  for (const auto& argument : arguments) {
    blob.append(argument);
    blob.push_back('\0');
  }
  return blob;
}

int child_mode(int argc, char** argv) {
  // This branch is entered through raz_rt_process_run_argv below.  It proves
  // the runtime forwards argv entries directly, without shell tokenization or
  // option interpretation.  Empty strings, spaces, quotes/backslashes and an
  // option-looking --help value must all arrive byte-for-byte unchanged.
  const std::vector<std::string> expected{
      "--native-argv-child", "alpha", "", "hello world", "--help", "quote\"slash\\"};
  if (argc != static_cast<int>(expected.size()) + 1) {
    std::cerr << "child argument count mismatch\n";
    return 91;
  }
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const std::string actual = argv[index + 1] == nullptr ? "" : argv[index + 1];
    if (actual != expected[index]) {
      std::cerr << "child argument mismatch at index " << index << "\n";
      return 92;
    }
  }
  if (!expect_runtime_arguments(argc, argv)) return 93;
  return 37;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && argv[1] != nullptr && std::strcmp(argv[1], "--native-argv-child") == 0) {
    return child_mode(argc, argv);
  }

  if (!expect_runtime_arguments(argc, argv)) return 1;
  if (argc <= 0 || argv[0] == nullptr || argv[0][0] == '\0') {
    std::cerr << "test executable path is unavailable\n";
    return 2;
  }

  const std::string program = argv[0];
  const std::vector<std::string> forwarded{
      "--native-argv-child", "alpha", "", "hello world", "--help", "quote\"slash\\"};
  const std::string blob = pack_arguments(forwarded);
  const auto status = raz_rt_process_run_argv(
      program.data(), static_cast<std::int64_t>(program.size()), blob.data(),
      static_cast<std::int64_t>(blob.size()), static_cast<std::int64_t>(forwarded.size()));
  if (status != 37) {
    std::cerr << "shell-free argv child returned " << status << " instead of 37\n";
    return 3;
  }

  // Malformed packed argv must be rejected before process creation.  The last
  // entry intentionally lacks its NUL terminator.
  const std::string malformed = "unterminated";
  if (raz_rt_process_run_argv(program.data(), static_cast<std::int64_t>(program.size()),
                              malformed.data(), static_cast<std::int64_t>(malformed.size()), 1) != -1) {
    std::cerr << "malformed packed argv was not rejected\n";
    return 4;
  }

  return 0;
}
