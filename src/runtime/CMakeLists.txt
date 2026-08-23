# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

add_library(raz_runtime STATIC
  runtime.cpp
  tls.cpp
  objects.cpp
  async.cpp
  core.cpp
  files_process.cpp
  network.cpp
  platform_threads_crypto.cpp
)
add_library(Raz::runtime ALIAS raz_runtime)
target_compile_features(raz_runtime PUBLIC cxx_std_20)
set_target_properties(raz_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Export the runtime's actual optional-link state into the top-level cache.
# The portable bootstrap runs after CMake returns, so it must not infer feature
# enablement from partial FindOpenSSL cache entries such as OPENSSL_INCLUDE_DIR.
# Those entries can exist on Windows even when no usable library pair was found.
set(RAZ_RUNTIME_OPENSSL_ENABLED OFF CACHE INTERNAL
  "Whether raz_runtime was compiled with OpenSSL support" FORCE)
set(_raz_runtime_link_deps "")

find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  set(RAZ_RUNTIME_OPENSSL_ENABLED ON CACHE INTERNAL
    "Whether raz_runtime was compiled with OpenSSL support" FORCE)
  target_compile_definitions(raz_runtime PRIVATE RAZ_HAVE_OPENSSL=1)
  target_link_libraries(raz_runtime PUBLIC OpenSSL::SSL OpenSSL::Crypto)

  # The recursive compiler stages are linked outside CMake by the portable
  # bootstrap driver. Export the exact linker inputs selected by CMake so that
  # driver never has to guess package-specific cache keys. TARGET_LINKER_FILE
  # is intentional: for a Windows DLL-backed OpenSSL install, TARGET_FILE is
  # the runtime DLL while the link step needs its import .lib.
  set(_raz_runtime_link_deps
    "$<TARGET_LINKER_FILE:OpenSSL::SSL>\n$<TARGET_LINKER_FILE:OpenSSL::Crypto>\n")
endif()

# Always generate the manifest, including the OpenSSL-disabled case. This makes
# stale/missing manifests distinguishable from a legitimate zero-dependency
# runtime and gives every bootstrap generation one deterministic contract.
file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/raz-runtime-link-deps.txt"
  CONTENT "${_raz_runtime_link_deps}")

if(WIN32)
  # Prevent legacy Windows SDK min/max macros from corrupting std::min,
  # std::max, and numeric_limits<T>::max() in this target and transitive
  # third-party Windows includes (notably OpenSSL).
  target_compile_definitions(raz_runtime PRIVATE NOMINMAX=1 WIN32_LEAN_AND_MEAN=1)
  target_link_libraries(raz_runtime PUBLIC ws2_32 bcrypt crypt32)
endif()

if(MSVC)
  # Raz executables are linked by ObLink first.  Keep the runtime free of the
  # MSVC compiler-injected security-cookie dependency so a normal runtime
  # archive does not require vcruntime merely because /GS is enabled by the
  # host compiler defaults.  Bounds/ownership safety is enforced by Raz and
  # explicit runtime checks; this switch only removes the host C++ stack-cookie
  # ABI edge from these boundary objects.
  target_compile_options(raz_runtime PRIVATE /W4 /permissive- /GS-)
  if(RAZ_ENABLE_WERROR)
    target_compile_options(raz_runtime PRIVATE /WX)
  endif()
else()
  target_compile_options(raz_runtime PRIVATE -Wall -Wextra -Wpedantic)
  if(RAZ_ENABLE_WERROR)
    target_compile_options(raz_runtime PRIVATE -Werror)
  endif()
endif()
