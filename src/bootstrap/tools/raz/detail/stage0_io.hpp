// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string json_escape(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 16);
  for (const unsigned char c : value) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result.push_back(static_cast<char>(c)); break;
    }
  }
  return result;
}
