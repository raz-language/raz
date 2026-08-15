// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace raz::terminal {

enum class ColorMode { auto_, always, never };

inline bool is_terminal(const std::ostream& stream) {
#if defined(_WIN32)
  if (&stream == &std::cout) return _isatty(_fileno(stdout)) != 0;
  if (&stream == &std::cerr) return _isatty(_fileno(stderr)) != 0;
  return false;
#else
  if (&stream == &std::cout) return ::isatty(STDOUT_FILENO) != 0;
  if (&stream == &std::cerr) return ::isatty(STDERR_FILENO) != 0;
  return false;
#endif
}

inline void enable_virtual_terminal_if_needed() {
#if defined(_WIN32)
  static bool initialized = false;
  if (initialized) return;
  initialized = true;
  for (const DWORD handle_id : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
    const HANDLE handle = GetStdHandle(handle_id);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) continue;
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) continue;
    SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#endif
}

inline bool color_enabled(const std::ostream& stream, ColorMode mode = ColorMode::auto_) {
  if (mode == ColorMode::never) return false;
  if (mode == ColorMode::always) {
    enable_virtual_terminal_if_needed();
    return true;
  }

  if (std::getenv("NO_COLOR") != nullptr) return false;
  const char* raz_color = std::getenv("RAZ_COLOR");
  if (raz_color != nullptr) {
    const std::string_view value(raz_color);
    if (value == "never" || value == "0" || value == "false") return false;
    if (value == "always" || value == "1" || value == "true") {
      enable_virtual_terminal_if_needed();
      return true;
    }
  }
  const bool enabled = is_terminal(stream);
  if (enabled) enable_virtual_terminal_if_needed();
  return enabled;
}

inline constexpr std::string_view reset = "\x1b[0m";
inline constexpr std::string_view bold = "\x1b[1m";
inline constexpr std::string_view dim = "\x1b[2m";
inline constexpr std::string_view red = "\x1b[31m";
inline constexpr std::string_view green = "\x1b[32m";
inline constexpr std::string_view yellow = "\x1b[33m";
inline constexpr std::string_view blue = "\x1b[34m";
inline constexpr std::string_view magenta = "\x1b[35m";
inline constexpr std::string_view cyan = "\x1b[36m";

inline void style(std::ostream& stream, bool enabled, std::string_view code,
                  std::string_view text) {
  if (enabled) stream << code;
  stream << text;
  if (enabled) stream << reset;
}

}  // namespace raz::terminal
