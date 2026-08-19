// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <istream>
#include <string>
#include <string_view>

// Windows tooling writes a UTF-8 BOM by default: Notepad, VS Code's "UTF-8 with
// BOM" setting, and Windows PowerShell 5.1's `Set-Content -Encoding utf8`,
// `Out-File -Encoding utf8`, and `>` redirection all emit EF BB BF. The marker
// carries no textual meaning, so manifests and `.rz` sources drop it on the way
// in and behave exactly like their BOM-less twins.
namespace raz::common {

inline constexpr std::string_view utf8_bom = "\xEF\xBB\xBF";

inline void strip_utf8_bom(std::string& text) {
  if (text.size() >= utf8_bom.size() && text.compare(0, utf8_bom.size(), utf8_bom) == 0) {
    text.erase(0, utf8_bom.size());
  }
}

// Consumes a leading BOM so the first `std::getline` sees the real first line.
// Leaves the stream untouched when the file does not start with one.
inline void skip_utf8_bom(std::istream& input) {
  if (!input.good()) {
    return;
  }

  const auto start = input.tellg();
  char marker[3] = {};
  if (!input.read(marker, static_cast<std::streamsize>(sizeof(marker)))) {
    input.clear();
    input.seekg(start);
    return;
  }
  if (std::string_view(marker, sizeof(marker)) == utf8_bom) {
    return;
  }
  input.seekg(start);
}

}  // namespace raz::common
