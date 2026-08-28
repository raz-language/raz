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
  web_dev.cpp
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

find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  set(RAZ_RUNTIME_OPENSSL_ENABLED ON CACHE INTERNAL
    "Whether raz_runtime was compiled with OpenSSL support" FORCE)
  target_compile_definitions(raz_runtime PRIVATE RAZ_HAVE_OPENSSL=1)
  target_link_libraries(raz_runtime PUBLIC OpenSSL::SSL OpenSSL::Crypto)

  # Bootstrap runs after the host CMake configure has completed and therefore
  # cannot rely on directory-scoped imported targets.  Persist Raz-owned linker
  # paths instead of depending on FindOpenSSL's implementation-detail cache
  # variables, which are not guaranteed to exist for every Windows discovery
  # path (for example config-package/vcpkg based installations).
  function(raz_runtime_record_imported_link_file target output_var)
    set(resolved "")
    foreach(property
        IMPORTED_IMPLIB_RELEASE IMPORTED_LOCATION_RELEASE
        IMPORTED_IMPLIB_RELWITHDEBINFO IMPORTED_LOCATION_RELWITHDEBINFO
        IMPORTED_IMPLIB_MINSIZEREL IMPORTED_LOCATION_MINSIZEREL
        IMPORTED_IMPLIB_DEBUG IMPORTED_LOCATION_DEBUG
        IMPORTED_IMPLIB IMPORTED_LOCATION)
      get_target_property(candidate ${target} ${property})
      if(candidate AND NOT candidate MATCHES "-NOTFOUND$")
        set(resolved "${candidate}")
        break()
      endif()
    endforeach()
    set(${output_var} "${resolved}" PARENT_SCOPE)
  endfunction()

  set(raz_runtime_ssl_link_file "${OPENSSL_SSL_LIBRARY}")
  if(NOT raz_runtime_ssl_link_file)
    raz_runtime_record_imported_link_file(OpenSSL::SSL raz_runtime_ssl_link_file)
  endif()
  set(raz_runtime_crypto_link_file "${OPENSSL_CRYPTO_LIBRARY}")
  if(NOT raz_runtime_crypto_link_file)
    raz_runtime_record_imported_link_file(OpenSSL::Crypto raz_runtime_crypto_link_file)
  endif()

  set(RAZ_RUNTIME_OPENSSL_SSL_LIBRARY "${raz_runtime_ssl_link_file}" CACHE FILEPATH
    "OpenSSL SSL linker file used by raz_runtime" FORCE)
  set(RAZ_RUNTIME_OPENSSL_CRYPTO_LIBRARY "${raz_runtime_crypto_link_file}" CACHE FILEPATH
    "OpenSSL Crypto linker file used by raz_runtime" FORCE)
endif()


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
  target_compile_options(raz_runtime PRIVATE /W4 /permissive- /GS- /Gy /Gw)
  if(RAZ_ENABLE_WERROR)
    target_compile_options(raz_runtime PRIVATE /WX)
  endif()
else()
  target_compile_options(raz_runtime PRIVATE -Wall -Wextra -Wpedantic)
  if(RAZ_ENABLE_WERROR)
    target_compile_options(raz_runtime PRIVATE -Werror)
  endif()
endif()
