// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

extern "C" std::int64_t raz_rt_tcp_listen_host(const char*, std::int64_t, std::int64_t, std::int64_t);
extern "C" std::int64_t raz_rt_socket_poll(std::int64_t, std::int64_t, std::int64_t);
extern "C" std::int64_t raz_rt_tcp_accept(std::int64_t);
extern "C" std::int64_t raz_rt_socket_send(std::int64_t, const void*, std::int64_t);
extern "C" std::int64_t raz_rt_socket_receive(std::int64_t, void*, std::int64_t);
extern "C" std::int64_t raz_rt_socket_set_timeout_millis(std::int64_t, std::int64_t, std::int64_t);
extern "C" void raz_rt_socket_close(std::int64_t);
extern "C" std::int64_t raz_rt_process_run_argv(const char*, std::int64_t, const char*, std::int64_t, std::int64_t);

namespace {

using Clock = std::chrono::steady_clock;

std::string text(const char* data, std::int64_t length) {
  if (data == nullptr || length <= 0) return {};
  return std::string(data, static_cast<std::size_t>(length));
}


std::string bundle_kind(const std::filesystem::path& relative) {
  const auto extension = relative.extension().string();
  const auto generic = relative.generic_string();
  if (extension == ".html") return "html  route/prerender";
  if (extension == ".css") return "css   stylesheet";
  if (extension == ".js" || extension == ".mjs") return "js    browser host/module";
  if (extension == ".wasm") {
    if (generic.rfind("assets/chunks/", 0) == 0) return "wasm  lazy split chunk";
    return "wasm  reachable Raz code/runtime";
  }
  if (relative.filename() == "asset-manifest.json") return "meta  logical-to-fingerprinted map";
  return "asset public/static asset";
}

int bundle_kind_index(const std::filesystem::path& relative) {
  const auto extension = relative.extension().string();
  if (extension == ".html") return 0;
  if (extension == ".css") return 1;
  if (extension == ".js" || extension == ".mjs") return 2;
  if (extension == ".wasm") return 3;
  if (relative.filename() == "asset-manifest.json") return 4;
  return 5;
}

std::string web_bundle_analysis(const std::filesystem::path& dist) {
  struct Row { std::filesystem::path relative; std::uintmax_t size; };
  std::vector<Row> rows;
  std::error_code error;
  std::filesystem::recursive_directory_iterator it(
      dist, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && it != end; it.increment(error)) {
    if (!it->is_regular_file(error)) { error.clear(); continue; }
    const auto relative = it->path().lexically_relative(dist);
    const auto size = it->file_size(error);
    if (error) { error.clear(); continue; }
    rows.push_back({relative, size});
  }
  if (error) return {};
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    return a.relative.generic_string() < b.relative.generic_string();
  });

  std::uintmax_t total = 0;
  std::uintmax_t category_bytes[6]{};
  std::uint64_t category_files[6]{};
  std::ostringstream out;
  out << "Raz web release bundle analysis\n\nFiles:\n";
  for (const auto& row : rows) {
    const auto index = bundle_kind_index(row.relative);
    total += row.size;
    category_bytes[index] += row.size;
    ++category_files[index];
    out << "  " << row.size << " B  " << bundle_kind(row.relative) << "  "
        << row.relative.generic_string() << "\n";
  }
  static constexpr const char* labels[6] = {
      "html  route/prerender", "css   stylesheet", "js    browser host/module",
      "wasm  reachable Raz code/runtime", "meta  logical-to-fingerprinted map",
      "asset public/static asset"};
  out << "\nSummary:\n  total  " << total << " B\n";
  for (int i = 0; i < 6; ++i) {
    out << "  " << labels[i] << ": " << category_files[i] << " files, "
        << category_bytes[i] << " B\n";
  }

  const auto manifest = dist / "asset-manifest.json";
  std::ifstream manifest_stream(manifest, std::ios::binary);
  if (manifest_stream) {
    std::ostringstream manifest_text;
    manifest_text << manifest_stream.rdbuf();
    out << "\nLogical asset map:\n" << manifest_text.str();
    if (!manifest_text.str().empty() && manifest_text.str().back() != '\n') out << '\n';
  }
  return out.str();
}

bool excluded_component(std::string_view name) {
  return name == ".git" || name == "dist" || name == "target" || name == "build" ||
         name == ".raz-cache" || name == ".idea" || name == ".vscode";
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
  return hash;
}

std::uint64_t watch_signature(const std::filesystem::path& root) {
  std::error_code error;
  if (!std::filesystem::exists(root, error)) return 0;
  std::uint64_t hash = 1469598103934665603ULL;
  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && it != end; it.increment(error)) {
    const auto& entry = *it;
    const auto name = entry.path().filename().string();
    if (entry.is_directory(error) && excluded_component(name)) {
      it.disable_recursion_pending();
      continue;
    }
    if (error || !entry.is_regular_file(error)) continue;
    const auto relative = entry.path().lexically_relative(root).generic_string();
    for (unsigned char ch : relative) hash = mix(hash, ch);
    const auto size = entry.file_size(error);
    if (!error) hash = mix(hash, static_cast<std::uint64_t>(size));
    const auto modified = entry.last_write_time(error);
    if (!error) {
      hash = mix(hash, static_cast<std::uint64_t>(modified.time_since_epoch().count()));
    }
    error.clear();
  }
  return hash;
}

bool send_all(std::int64_t socket, const char* data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const auto count = raz_rt_socket_send(socket, data + sent, static_cast<std::int64_t>(size - sent));
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}

std::string mime_type(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (extension == ".html" || extension == ".htm") return "text/html; charset=utf-8";
  if (extension == ".css") return "text/css; charset=utf-8";
  if (extension == ".js" || extension == ".mjs") return "text/javascript; charset=utf-8";
  if (extension == ".wasm") return "application/wasm";
  if (extension == ".json" || extension == ".map") return "application/json; charset=utf-8";
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".png") return "image/png";
  if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
  if (extension == ".gif") return "image/gif";
  if (extension == ".webp") return "image/webp";
  if (extension == ".ico") return "image/x-icon";
  if (extension == ".woff") return "font/woff";
  if (extension == ".woff2") return "font/woff2";
  if (extension == ".txt") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string decode_path(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const int high = hex_value(input[i + 1]);
      const int low = hex_value(input[i + 2]);
      if (high >= 0 && low >= 0) {
        const char value = static_cast<char>((high << 4) | low);
        if (value == '\0') return {};
        output.push_back(value);
        i += 2;
        continue;
      }
    }
    output.push_back(input[i]);
  }
  return output;
}

bool safe_relative_path(std::string_view path) {
  if (path.empty() || path.front() != '/') return false;
  std::filesystem::path relative(path.substr(1));
  if (relative.is_absolute()) return false;
  for (const auto& component : relative) {
    if (component == "..") return false;
  }
  return true;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  stream.seekg(0, std::ios::end);
  const auto end = stream.tellg();
  if (end < 0) return {};
  std::string bytes(static_cast<std::size_t>(end), '\0');
  stream.seekg(0, std::ios::beg);
  if (!bytes.empty()) stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return stream || bytes.empty() ? bytes : std::string{};
}

void send_response(std::int64_t socket, int status, std::string_view reason,
                   std::string_view type, std::string_view body, bool head_only = false) {
  std::ostringstream header;
  header << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: no-store, no-cache, must-revalidate\r\n"
         << "Connection: close\r\n"
         << "X-Content-Type-Options: nosniff\r\n\r\n";
  const auto headers = header.str();
  send_all(socket, headers.data(), headers.size());
  if (!head_only && !body.empty()) send_all(socket, body.data(), body.size());
}


std::uint64_t dist_signature(const std::filesystem::path& root, int kind) {
  std::error_code error;
  if (!std::filesystem::exists(root, error)) return 0;
  std::uint64_t hash = 1469598103934665603ULL;
  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && it != end; it.increment(error)) {
    const auto& entry = *it;
    if (error || !entry.is_regular_file(error)) continue;
    std::string extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    bool include = false;
    if (kind == 1) include = extension == ".css";
    else if (kind == 2) include = extension == ".js" || extension == ".mjs" || extension == ".wasm";
    else if (kind == 3) include = extension == ".html" || extension == ".htm";
    if (!include) continue;
    const auto relative = entry.path().lexically_relative(root).generic_string();
    for (unsigned char ch : relative) hash = mix(hash, ch);
    std::ifstream stream(entry.path(), std::ios::binary);
    char buffer[16384];
    while (stream) {
      stream.read(buffer, static_cast<std::streamsize>(sizeof(buffer)));
      const auto count = stream.gcount();
      for (std::streamsize i = 0; i < count; ++i) {
        hash = mix(hash, static_cast<unsigned char>(buffer[i]));
      }
    }
    error.clear();
  }
  return hash;
}

const char* reload_kind_name(int kind) {
  return kind == 2 ? "css" : "full";
}

std::string inject_live_reload(std::string html, std::uint64_t version) {
  const std::string script =
      "<script>(()=>{let v=" + std::to_string(version) +
      ";const id='__raz_build_error';"
      "const clear=()=>{const e=document.getElementById(id);if(e)e.remove();};"
      "const fail=(ms)=>{let e=document.getElementById(id);if(!e){e=document.createElement('div');"
      "e.id=id;e.style='position:fixed;left:0;right:0;bottom:0;z-index:2147483647;padding:12px 16px;"
      "background:#7f1d1d;color:white;font:14px/1.4 system-ui,sans-serif;box-shadow:0 -2px 8px #0004';"
      "document.documentElement.appendChild(e);}e.textContent='Raz build failed'+(ms>0?' after '+ms+'ms':'')+' — see the terminal for diagnostics';};"
      "const css=(n)=>{document.querySelectorAll('link[rel~=stylesheet]').forEach((e)=>{try{const u=new URL(e.href,location.href);u.searchParams.set('__raz',String(n));e.href=u.href;}catch(_){}});};"
      "setInterval(async()=>{try{const r=await fetch('/__raz/status.json',{cache:'no-store'});const s=await r.json();"
      "if(s.failed){fail(s.build_ms||0);return;}clear();if(s.version!==v){v=s.version;if(s.reload==='css')css(v);else location.reload();}}catch(_){}},250);})();</script>";
  const auto body = html.rfind("</body>");
  if (body != std::string::npos) html.insert(body, script);
  else html += script;
  return html;
}

void serve_client(std::int64_t client, const std::filesystem::path& dist,
                  std::uint64_t version, bool build_failed, std::int64_t build_millis,
                  int reload_kind) {
  char request[16384];
  const auto received = raz_rt_socket_receive(client, request, static_cast<std::int64_t>(sizeof(request) - 1));
  if (received <= 0) return;
  request[received] = '\0';
  std::string_view line(request, static_cast<std::size_t>(received));
  const auto eol = line.find("\r\n");
  if (eol != std::string_view::npos) line = line.substr(0, eol);
  const auto first_space = line.find(' ');
  const auto second_space = first_space == std::string_view::npos ? std::string_view::npos : line.find(' ', first_space + 1);
  if (first_space == std::string_view::npos || second_space == std::string_view::npos) {
    send_response(client, 400, "Bad Request", "text/plain; charset=utf-8", "Bad Request\n");
    return;
  }
  const auto method = line.substr(0, first_space);
  if (method != "GET" && method != "HEAD") {
    send_response(client, 405, "Method Not Allowed", "text/plain; charset=utf-8", "Method Not Allowed\n", method == "HEAD");
    return;
  }
  std::string raw(line.substr(first_space + 1, second_space - first_space - 1));
  if (const auto query = raw.find_first_of("?#"); query != std::string::npos) raw.resize(query);
  const std::string path = decode_path(raw);
  if (!safe_relative_path(path)) {
    send_response(client, 400, "Bad Request", "text/plain; charset=utf-8", "Bad Request\n", method == "HEAD");
    return;
  }
  if (path == "/__raz/version") {
    const auto body = std::to_string(version);
    send_response(client, 200, "OK", "text/plain; charset=utf-8", body, method == "HEAD");
    return;
  }
  if (path == "/__raz/status") {
    const auto body = std::to_string(version) + ":" + (build_failed ? "1" : "0");
    send_response(client, 200, "OK", "text/plain; charset=utf-8", body, method == "HEAD");
    return;
  }
  if (path == "/__raz/status.json") {
    const auto body = std::string("{\"version\":") + std::to_string(version) +
        ",\"failed\":" + (build_failed ? "true" : "false") +
        ",\"build_ms\":" + std::to_string(build_millis) +
        ",\"reload\":\"" + reload_kind_name(reload_kind) + "\"}";
    send_response(client, 200, "OK", "application/json; charset=utf-8", body, method == "HEAD");
    return;
  }

  std::filesystem::path file = dist / std::filesystem::path(path.substr(1));
  std::error_code error;
  if (path == "/" || std::filesystem::is_directory(file, error)) file /= "index.html";
  error.clear();
  bool exists = std::filesystem::is_regular_file(file, error);
  if (!exists) {
    // Browser-side Raz routing uses the History API. Unknown extensionless paths
    // therefore fall back to the application shell while actual missing assets
    // continue to return 404.
    const std::filesystem::path requested(path);
    if (requested.extension().empty()) {
      file = dist / "index.html";
      exists = std::filesystem::is_regular_file(file, error);
    }
  }
  if (!exists) {
    send_response(client, 404, "Not Found", "text/plain; charset=utf-8", "Not Found\n", method == "HEAD");
    return;
  }

  std::string body = read_file(file);
  if (file.extension() == ".html") body = inject_live_reload(std::move(body), version);
  send_response(client, 200, "OK", mime_type(file), body, method == "HEAD");
}

std::int64_t rebuild(const std::string& compiler, const std::string& manifest) {
  std::string arguments;
  arguments.append("build", 5);
  arguments.push_back('\0');
  arguments.append(manifest);
  arguments.push_back('\0');
  return raz_rt_process_run_argv(compiler.data(), static_cast<std::int64_t>(compiler.size()),
                                 arguments.data(), static_cast<std::int64_t>(arguments.size()), 2);
}

}  // namespace

extern "C" std::int64_t raz_rt_web_bundle_analyze(
    const char* dist_data, std::int64_t dist_length,
    const char* manifest_data, std::int64_t manifest_length) {
  if (dist_data == nullptr || dist_length <= 0 || manifest_data == nullptr || manifest_length <= 0) return -1;
  const std::filesystem::path dist(text(dist_data, dist_length));
  const std::filesystem::path manifest(text(manifest_data, manifest_length));
  std::error_code error;
  if (!std::filesystem::is_directory(dist, error) || error) return -1;
  const auto report = web_bundle_analysis(dist);
  if (report.empty()) return -1;
  const auto report_dir = manifest.parent_path() / "target" / "release";
  std::filesystem::create_directories(report_dir, error);
  if (error) return -1;
  const auto report_path = report_dir / "web-bundle-analysis.txt";
  std::ofstream stream(report_path, std::ios::binary | std::ios::trunc);
  if (!stream) return -1;
  stream.write(report.data(), static_cast<std::streamsize>(report.size()));
  if (!stream) return -1;
  stream.close();
  std::fwrite(report.data(), 1, report.size(), stdout);
  std::printf("\nAnalysis written to target/release/web-bundle-analysis.txt\n");
  std::fflush(stdout);
  return 0;
}

extern "C" std::int64_t raz_rt_web_dev_server(
    const char* compiler_data, std::int64_t compiler_length,
    const char* manifest_data, std::int64_t manifest_length,
    const char* root_data, std::int64_t root_length,
    const char* dist_data, std::int64_t dist_length,
    const char* host_data, std::int64_t host_length,
    std::int64_t port) {
  const std::string compiler = text(compiler_data, compiler_length);
  const std::string manifest = text(manifest_data, manifest_length);
  const std::filesystem::path root(text(root_data, root_length));
  const std::filesystem::path dist(text(dist_data, dist_length));
  const std::string host = text(host_data, host_length);
  if (compiler.empty() || manifest.empty() || root.empty() || dist.empty() || host.empty() ||
      port <= 0 || port > 65535) return -1;

  const auto listener = raz_rt_tcp_listen_host(host.c_str(), static_cast<std::int64_t>(host.size()), port, 64);
  if (listener < 0) return -1;

  std::printf("    Serving http://%s:%lld\n", host.c_str(), static_cast<long long>(port));
  std::printf("    Watching %s\n", root.string().c_str());
  std::fflush(stdout);

  std::uint64_t signature = watch_signature(root);
  std::uint64_t version = 1;
  bool last_build_failed = false;
  std::int64_t last_build_millis = 0;
  int last_reload_kind = 1;
  auto changed_at = Clock::time_point{};
  auto next_watch = Clock::now();
  bool pending = false;
  std::atomic<bool> rebuild_done{false};
  std::atomic<std::int64_t> rebuild_status{-1};
  std::atomic<std::int64_t> rebuild_millis{0};
  std::atomic<int> rebuild_reload_kind{1};
  bool rebuild_running = false;
  std::thread rebuild_thread;

  unsigned poll_failures = 0;
  for (;;) {
    const auto events = raz_rt_socket_poll(listener, 1, 100);
    if (events < 0) {
      // A completed rebuild child can interrupt poll on POSIX. Treat isolated
      // interruptions as transient; a genuinely broken listener will fail
      // repeatedly and still terminate instead of spinning forever.
      if (++poll_failures < 16U) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      raz_rt_socket_close(listener);
      return -1;
    }
    poll_failures = 0;
    if ((events & 1) != 0) {
      const auto client = raz_rt_tcp_accept(listener);
      if (client >= 0) {
        // A half-open browser/probe must never stall the watch loop. Development
        // requests are tiny, so a short receive/send timeout is sufficient.
        raz_rt_socket_set_timeout_millis(client, 250, 1000);
        serve_client(client, dist, version, last_build_failed, last_build_millis, last_reload_kind);
        raz_rt_socket_close(client);
      }
    }

    if (rebuild_running && rebuild_done.load(std::memory_order_acquire)) {
      rebuild_thread.join();
      rebuild_running = false;
      const auto status = rebuild_status.load(std::memory_order_relaxed);
      last_build_millis = rebuild_millis.load(std::memory_order_relaxed);
      if (status == 0) {
        last_build_failed = false;
        last_reload_kind = rebuild_reload_kind.load(std::memory_order_relaxed);
        ++version;
        signature = watch_signature(root);
        std::printf("    Reloaded http://%s:%lld in %lld ms (%s)\n", host.c_str(),
                    static_cast<long long>(port), static_cast<long long>(last_build_millis),
                    reload_kind_name(last_reload_kind));
      } else {
        last_build_failed = true;
        std::printf("      Build failed after %lld ms; serving the last successful bundle\n",
                    static_cast<long long>(last_build_millis));
      }
      std::fflush(stdout);
    }

    const auto now = Clock::now();
    if (now >= next_watch) {
      next_watch = now + std::chrono::milliseconds(120);
      const std::uint64_t next = watch_signature(root);
      if (next != signature) {
        signature = next;
        changed_at = now;
        pending = true;
      }
    }
    if (
        pending && !rebuild_running &&
        Clock::now() - changed_at >= std::chrono::milliseconds(150)
    ) {
      pending = false;
      rebuild_done.store(false, std::memory_order_release);
      rebuild_status.store(-1, std::memory_order_relaxed);
      rebuild_millis.store(0, std::memory_order_relaxed);
      rebuild_reload_kind.store(1, std::memory_order_relaxed);
      rebuild_running = true;
      const std::uint64_t css_before = dist_signature(dist, 1);
      const std::uint64_t script_before = dist_signature(dist, 2);
      const std::uint64_t html_before = dist_signature(dist, 3);
      std::printf("   Rebuilding %s\n", manifest.c_str());
      std::fflush(stdout);
      rebuild_thread = std::thread([&, css_before, script_before, html_before]() {
        const auto started = Clock::now();
        const auto status = rebuild(compiler, manifest);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        int kind = 1;
        if (status == 0) {
          const auto css_after = dist_signature(dist, 1);
          const auto script_after = dist_signature(dist, 2);
          const auto html_after = dist_signature(dist, 3);
          if (css_after != css_before && script_after == script_before && html_after == html_before) kind = 2;
        }
        rebuild_reload_kind.store(kind, std::memory_order_relaxed);
        rebuild_millis.store(static_cast<std::int64_t>(elapsed), std::memory_order_relaxed);
        rebuild_status.store(status, std::memory_order_relaxed);
        rebuild_done.store(true, std::memory_order_release);
      });
    }
  }
}
