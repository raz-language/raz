// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

extern "C" {
std::int64_t raz_rt_dns_resolve_ipv4(const char* host, std::int64_t host_length, char* output, std::int64_t capacity) {
#if defined(_WIN32)
  if (!winsock().ok) return -1;
#endif
  const auto name = view_text(host, host_length);
  if (name.empty()) return -1;
  addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  if (getaddrinfo(name.c_str(), nullptr, &hints, &results) != 0 || results == nullptr) return -1;
  char address[INET_ADDRSTRLEN]{};
  const auto* ipv4 = reinterpret_cast<sockaddr_in*>(results->ai_addr);
  const char* converted = inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address));
  freeaddrinfo(results);
  return converted == nullptr ? -1 : copy_text(address, output, capacity);
}


// Compact, allocation-free-per-record DNS result ABI used by the Raz resolver.
// family is 4 or 6; IPv4 uses low's low 32 bits in canonical network order.
// IPv6 high/low are canonical big-endian numeric halves, independent of host endian.
struct RazResolvedAddressRecord {
  std::int64_t family;
  std::uint64_t high;
  std::uint64_t low;
  std::int64_t scope_id;
};
static_assert(sizeof(RazResolvedAddressRecord) == 32);

static bool raz_resolved_record(const sockaddr* address, RazResolvedAddressRecord& output) {
  if (address == nullptr) return false;
  if (address->sa_family == AF_INET) {
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
    output.family = 4;
    output.high = 0;
    output.low = static_cast<std::uint64_t>(ntohl(ipv4->sin_addr.s_addr));
    output.scope_id = 0;
    return true;
  }
#if defined(AF_INET6)
  if (address->sa_family == AF_INET6) {
    const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    for (int index = 0; index < 8; ++index) high = (high << 8U) | ipv6->sin6_addr.s6_addr[index];
    for (int index = 8; index < 16; ++index) low = (low << 8U) | ipv6->sin6_addr.s6_addr[index];
    output.family = 6;
    output.high = high;
    output.low = low;
    output.scope_id = static_cast<std::int64_t>(ipv6->sin6_scope_id);
    return true;
  }
#endif
  return false;
}

static bool raz_same_resolved(const RazResolvedAddressRecord& left, const RazResolvedAddressRecord& right) {
  return left.family == right.family && left.high == right.high && left.low == right.low && left.scope_id == right.scope_id;
}

std::int64_t raz_rt_dns_resolve_all(const char* host, std::int64_t host_length,
                                     RazResolvedAddressRecord* output, std::int64_t capacity) {
#if defined(_WIN32)
  if (!winsock().ok) return -1;
#endif
  if (capacity < 0) return -1;
  const auto name = view_text(host, host_length);
  if (name.empty()) return -1;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const int status = getaddrinfo(name.c_str(), nullptr, &hints, &results);
  if (status != 0 || results == nullptr) return -1;

  std::vector<RazResolvedAddressRecord> unique;
  for (auto* current = results; current != nullptr; current = current->ai_next) {
    RazResolvedAddressRecord record{};
    if (!raz_resolved_record(current->ai_addr, record)) continue;
    bool duplicate = false;
    for (const auto& existing : unique) {
      if (raz_same_resolved(existing, record)) { duplicate = true; break; }
    }
    if (!duplicate) unique.push_back(record);
  }

  freeaddrinfo(results);
  if (unique.empty()) return -1;

  const auto required = static_cast<std::int64_t>(unique.size());
  if (output != nullptr && capacity > 0) {
    const auto count = std::min<std::int64_t>(capacity, required);
    std::memcpy(output, unique.data(), static_cast<std::size_t>(count) * sizeof(RazResolvedAddressRecord));
  }
  return required;
}

std::int64_t raz_rt_socket_family(std::int64_t socket_value) {
  sockaddr_storage address{};
#if defined(_WIN32)
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  if (::getsockname(static_cast<Socket>(socket_value), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
  if (address.ss_family == AF_INET) return 4;
#if defined(AF_INET6)
  if (address.ss_family == AF_INET6) return 6;
#endif
  return 0;
}

std::int64_t socket_endpoint_text(std::int64_t socket_value, bool peer, char* output, std::int64_t capacity) {
  sockaddr_storage address{};
#if defined(_WIN32)
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  const int status = peer
      ? ::getpeername(static_cast<Socket>(socket_value), reinterpret_cast<sockaddr*>(&address), &length)
      : ::getsockname(static_cast<Socket>(socket_value), reinterpret_cast<sockaddr*>(&address), &length);
  if (status != 0) return -1;
  char text[INET6_ADDRSTRLEN]{};
  const void* source = nullptr;
  if (address.ss_family == AF_INET) source = &reinterpret_cast<sockaddr_in*>(&address)->sin_addr;
#if defined(AF_INET6)
  else if (address.ss_family == AF_INET6) source = &reinterpret_cast<sockaddr_in6*>(&address)->sin6_addr;
#endif
  if (source == nullptr || inet_ntop(address.ss_family, source, text, sizeof(text)) == nullptr) return -1;
  return copy_text(text, output, capacity);
}

std::int64_t socket_endpoint_port(std::int64_t socket_value, bool peer) {
  sockaddr_storage address{};
#if defined(_WIN32)
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  const int status = peer
      ? ::getpeername(static_cast<Socket>(socket_value), reinterpret_cast<sockaddr*>(&address), &length)
      : ::getsockname(static_cast<Socket>(socket_value), reinterpret_cast<sockaddr*>(&address), &length);
  if (status != 0) return -1;
  if (address.ss_family == AF_INET) return ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
#if defined(AF_INET6)
  if (address.ss_family == AF_INET6) return ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port);
#endif
  return -1;
}

static std::int64_t raz_bind_socket_host(const char* host, std::int64_t host_length, std::int64_t port,
                                          int socket_type, int protocol, std::int64_t backlog) {
#if defined(_WIN32)
  if (!winsock().ok) { raz_set_last_error(-1); return -1; }
#endif
  if (port < 0 || port > 65535) { raz_set_last_error(EINVAL); return -1; }
  std::string host_text;
  const char* host_ptr = nullptr;
  if (host != nullptr && host_length > 0) {
    host_text = view_text(host, host_length);
    if (host_text.empty()) { raz_set_last_error(EINVAL); return -1; }
    host_ptr = host_text.c_str();
  }
  const auto service = std::to_string(port);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socket_type;
  hints.ai_protocol = protocol;
  hints.ai_flags = AI_PASSIVE;
  addrinfo* results = nullptr;
  const int lookup_status = getaddrinfo(host_ptr, service.c_str(), &hints, &results);
  if (lookup_status != 0 || results == nullptr) { raz_set_last_error(lookup_status); return -1; }

  Socket bound = invalid_socket;
  std::int64_t bind_error = -1;
  for (auto* current = results; current != nullptr; current = current->ai_next) {
    Socket candidate = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
    if (candidate == invalid_socket) { raz_set_socket_error(); bind_error = raz_last_error_code; continue; }
    int enabled = 1;
    setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#if defined(IPV6_V6ONLY)
    if (current->ai_family == AF_INET6 && host_ptr == nullptr) {
      int disabled = 0;
      setsockopt(candidate, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&disabled), sizeof(disabled));
    }
#endif
    if (::bind(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
      if (socket_type != SOCK_STREAM || ::listen(candidate, static_cast<int>(backlog)) == 0) {
        bound = candidate;
        break;
      }
    }
    raz_set_socket_error();
    bind_error = raz_last_error_code;
    close_socket(candidate);
  }

  freeaddrinfo(results);
  if (bound == invalid_socket) { raz_set_last_error(bind_error); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(bound);
}

std::int64_t raz_rt_udp_bind_host(const char* host, std::int64_t host_length, std::int64_t port) {
  return raz_bind_socket_host(host, host_length, port, SOCK_DGRAM, IPPROTO_UDP, 0);
}

std::int64_t raz_rt_tcp_listen_host(const char* host, std::int64_t host_length, std::int64_t port, std::int64_t backlog) {
  if (backlog <= 0) { raz_set_last_error(EINVAL); return -1; }
  return raz_bind_socket_host(host, host_length, port, SOCK_STREAM, IPPROTO_TCP, backlog);
}

std::int64_t raz_rt_udp_bind(std::int64_t port) {
#if defined(_WIN32)
  if (!winsock().ok) { raz_set_last_error(-1); return -1; }
#endif
  if (port < 0 || port > 65535) { raz_set_last_error(EINVAL); return -1; }
  Socket socket_value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_value == invalid_socket) { raz_set_socket_error(); return -1; }
  int enabled = 1;
  setsockopt(socket_value, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(port));
  if (::bind(socket_value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    raz_set_socket_error();
    close_socket(socket_value);
    return -1;
  }

  raz_clear_last_error();
  return static_cast<std::int64_t>(socket_value);
}

std::int64_t raz_rt_udp_connect(const char* host, std::int64_t host_length, std::int64_t port) {
#if defined(_WIN32)
  if (!winsock().ok) { raz_set_last_error(-1); return -1; }
#endif
  if (port < 0 || port > 65535) { raz_set_last_error(EINVAL); return -1; }
  const auto name = view_text(host, host_length);
  if (name.empty()) { raz_set_last_error(EINVAL); return -1; }
  const auto service = std::to_string(port);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  addrinfo* results = nullptr;
  const int lookup_status = getaddrinfo(name.c_str(), service.c_str(), &hints, &results);
  if (lookup_status != 0 || results == nullptr) { raz_set_last_error(lookup_status); return -1; }
  Socket connected = invalid_socket;
  std::int64_t connect_error = -1;
  for (auto* current = results; current != nullptr; current = current->ai_next) {
    Socket candidate = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
    if (candidate == invalid_socket) { raz_set_socket_error(); connect_error = raz_last_error_code; continue; }
    if (::connect(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
      connected = candidate;
      break;
    }
    raz_set_socket_error();
    connect_error = raz_last_error_code;
    close_socket(candidate);
  }

  freeaddrinfo(results);
  if (connected == invalid_socket) { raz_set_last_error(connect_error); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(connected);
}

std::int64_t raz_rt_socket_local_port(std::int64_t socket_value) {
  return socket_endpoint_port(socket_value, false);
}

std::int64_t raz_rt_socket_peer_port(std::int64_t socket_value) {
  return socket_endpoint_port(socket_value, true);
}

std::int64_t raz_rt_socket_local_address(std::int64_t socket_value, char* output, std::int64_t capacity) {
  return socket_endpoint_text(socket_value, false, output, capacity);
}

std::int64_t raz_rt_socket_peer_address(std::int64_t socket_value, char* output, std::int64_t capacity) {
  return socket_endpoint_text(socket_value, true, output, capacity);
}

std::int64_t raz_rt_socket_set_nonblocking(std::int64_t socket_value, std::int64_t enabled) {
#if defined(_WIN32)
  u_long mode = enabled != 0 ? 1UL : 0UL;
  return ioctlsocket(static_cast<Socket>(socket_value), FIONBIO, &mode) == 0 ? 1 : 0;
#else
  const int current = fcntl(static_cast<Socket>(socket_value), F_GETFL, 0);
  if (current < 0) return 0;
  const int updated = enabled != 0 ? (current | O_NONBLOCK) : (current & ~O_NONBLOCK);
  return fcntl(static_cast<Socket>(socket_value), F_SETFL, updated) == 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_socket_shutdown(std::int64_t socket_value, std::int64_t how) {
  int native_how = 2;
  if (how == 0) native_how = 0;
  else if (how == 1) native_how = 1;
#if defined(_WIN32)
  return ::shutdown(static_cast<Socket>(socket_value), native_how) == 0 ? 1 : 0;
#else
  return ::shutdown(static_cast<Socket>(socket_value), native_how) == 0 ? 1 : 0;
#endif
}

std::int64_t raz_rt_socket_set_nodelay(std::int64_t socket_value, std::int64_t enabled) {
  const int value = enabled != 0 ? 1 : 0;
  return setsockopt(static_cast<Socket>(socket_value), IPPROTO_TCP, TCP_NODELAY,
                    reinterpret_cast<const char*>(&value), sizeof(value)) == 0 ? 1 : 0;
}

std::int64_t raz_rt_socket_set_keepalive(std::int64_t socket_value, std::int64_t enabled) {
  const int value = enabled != 0 ? 1 : 0;
  return setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_KEEPALIVE,
                    reinterpret_cast<const char*>(&value), sizeof(value)) == 0 ? 1 : 0;
}

std::int64_t raz_rt_socket_set_reuse_address(std::int64_t socket_value, std::int64_t enabled) {
  const int value = enabled != 0 ? 1 : 0;
  return setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_REUSEADDR,
                    reinterpret_cast<const char*>(&value), sizeof(value)) == 0 ? 1 : 0;
}


std::int64_t raz_rt_socket_set_reuse_port(std::int64_t socket_value, std::int64_t enabled) {
#if defined(SO_REUSEPORT)
  const int value = enabled != 0 ? 1 : 0;
  if (setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_REUSEPORT,
                 reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
    raz_set_socket_error(); return 0;
  }
  raz_clear_last_error(); return 1;
#else
  (void)socket_value; (void)enabled; raz_set_last_error(ENOTSUP); return 0;
#endif
}

std::int64_t raz_rt_socket_set_broadcast(std::int64_t socket_value, std::int64_t enabled) {
  const int value = enabled != 0 ? 1 : 0;
  if (setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_BROADCAST,
                 reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
    raz_set_socket_error(); return 0;
  }
  raz_clear_last_error(); return 1;
}

std::int64_t raz_rt_udp_multicast_ipv4(std::int64_t socket_value, const char* group,
                                        std::int64_t group_length, const char* interface_address,
                                        std::int64_t interface_length, std::int64_t join) {
  const auto group_text = view_text(group, group_length);
  if (group_text.empty()) { raz_set_last_error(EINVAL); return 0; }
  ip_mreq request{};
  if (inet_pton(AF_INET, group_text.c_str(), &request.imr_multiaddr) != 1) {
    raz_set_last_error(EINVAL); return 0;
  }
  const auto interface_text = view_text(interface_address, interface_length);
  if (interface_text.empty()) request.imr_interface.s_addr = htonl(INADDR_ANY);
  else if (inet_pton(AF_INET, interface_text.c_str(), &request.imr_interface) != 1) {
    raz_set_last_error(EINVAL); return 0;
  }
  const int option = join != 0 ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
  if (setsockopt(static_cast<Socket>(socket_value), IPPROTO_IP, option,
                 reinterpret_cast<const char*>(&request), sizeof(request)) != 0) {
    raz_set_socket_error(); return 0;
  }
  raz_clear_last_error(); return 1;
}

std::int64_t raz_rt_socket_set_buffer_sizes(std::int64_t socket_value, std::int64_t receive_size,
                                             std::int64_t send_size) {
  if (receive_size <= 0 || send_size <= 0) return 0;
  const int receive_value = static_cast<int>(receive_size);
  const int send_value = static_cast<int>(send_size);
  const bool receive_ok = setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_RCVBUF,
      reinterpret_cast<const char*>(&receive_value), sizeof(receive_value)) == 0;
  const bool send_ok = setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_SNDBUF,
      reinterpret_cast<const char*>(&send_value), sizeof(send_value)) == 0;
  return receive_ok && send_ok ? 1 : 0;
}

std::int64_t raz_rt_socket_set_timeout_millis(std::int64_t socket_value, std::int64_t receive_millis,
                                               std::int64_t send_millis) {
  if (receive_millis < 0 || send_millis < 0) return 0;
#if defined(_WIN32)
  const DWORD receive_value = static_cast<DWORD>(receive_millis);
  const DWORD send_value = static_cast<DWORD>(send_millis);
#else
  const timeval receive_value{static_cast<time_t>(receive_millis / 1000), static_cast<suseconds_t>((receive_millis % 1000) * 1000)};
  const timeval send_value{static_cast<time_t>(send_millis / 1000), static_cast<suseconds_t>((send_millis % 1000) * 1000)};
#endif
  const bool receive_ok = setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_RCVTIMEO,
      reinterpret_cast<const char*>(&receive_value), sizeof(receive_value)) == 0;
  const bool send_ok = setsockopt(static_cast<Socket>(socket_value), SOL_SOCKET, SO_SNDTIMEO,
      reinterpret_cast<const char*>(&send_value), sizeof(send_value)) == 0;
  return receive_ok && send_ok ? 1 : 0;
}

std::int64_t raz_rt_udp_send_to(std::int64_t socket_value, const char* host, std::int64_t host_length,
                                  std::int64_t port, const void* data, std::int64_t size) {
#if defined(_WIN32)
  if (!winsock().ok) { raz_set_last_error(-1); return -1; }
#endif
  if (data == nullptr || size < 0 || port < 0 || port > 65535) { raz_set_last_error(EINVAL); return -1; }
  const auto name = view_text(host, host_length);
  if (name.empty()) { raz_set_last_error(EINVAL); return -1; }
  const auto service = std::to_string(port);
  addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_DGRAM;
  addrinfo* results = nullptr;
  const int lookup_status = getaddrinfo(name.c_str(), service.c_str(), &hints, &results);
  if (lookup_status != 0 || results == nullptr) { raz_set_last_error(lookup_status); return -1; }
  std::int64_t sent = -1;
  std::int64_t send_error = -1;
  for (auto* current = results; current != nullptr; current = current->ai_next) {
#if defined(_WIN32)
    const int result = ::sendto(static_cast<Socket>(socket_value), static_cast<const char*>(data),
                                static_cast<int>(size), 0, current->ai_addr, static_cast<int>(current->ai_addrlen));
#else
    const auto result = ::sendto(static_cast<Socket>(socket_value), data, static_cast<std::size_t>(size), 0,
                                 current->ai_addr, current->ai_addrlen);
#endif
    if (result >= 0) { sent = static_cast<std::int64_t>(result); break; }
    raz_set_socket_error();
    send_error = raz_last_error_code;
  }

  freeaddrinfo(results);
  if (sent < 0) { raz_set_last_error(send_error); return -1; }
  raz_clear_last_error();
  return sent;
}

std::int64_t raz_rt_udp_receive_from(std::int64_t socket_value, void* data, std::int64_t capacity,
                                      char* address_output, std::int64_t address_capacity,
                                      std::int64_t* port_output) {
  if (data == nullptr || capacity < 0) { raz_set_last_error(EINVAL); return -1; }
  sockaddr_storage source{};
#if defined(_WIN32)
  int source_length = sizeof(source);
  const int result = ::recvfrom(static_cast<Socket>(socket_value), static_cast<char*>(data),
                                static_cast<int>(capacity), 0, reinterpret_cast<sockaddr*>(&source), &source_length);
#else
  socklen_t source_length = sizeof(source);
  const auto result = ::recvfrom(static_cast<Socket>(socket_value), data, static_cast<std::size_t>(capacity), 0,
                                 reinterpret_cast<sockaddr*>(&source), &source_length);
#endif
  if (result < 0) { raz_set_socket_error(); return -1; }
  char text[INET6_ADDRSTRLEN]{};
  const void* raw_address = nullptr;
  std::int64_t port = -1;
  if (source.ss_family == AF_INET) {
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&source);
    raw_address = &ipv4->sin_addr;
    port = ntohs(ipv4->sin_port);
  }
#if defined(AF_INET6)
  else if (source.ss_family == AF_INET6) {
    const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&source);
    raw_address = &ipv6->sin6_addr;
    port = ntohs(ipv6->sin6_port);
  }
#endif
  if (port_output != nullptr) *port_output = port;
  if (raw_address != nullptr && inet_ntop(source.ss_family, raw_address, text, sizeof(text)) != nullptr) {
    copy_text(text, address_output, address_capacity);
  } else if (address_output != nullptr && address_capacity > 0) {
    address_output[0] = '\0';
  }

  raz_clear_last_error();
  return static_cast<std::int64_t>(result);
}

std::int64_t raz_rt_tcp_connect(const char* host, std::int64_t host_length, std::int64_t port) {
#if defined(_WIN32)
  if (!winsock().ok) { raz_set_socket_error(); return -1; }
#endif
  if (port < 0 || port > 65535) { raz_set_last_error(EINVAL); return -1; }
  const auto name = view_text(host, host_length);
  if (name.empty()) { raz_set_last_error(EINVAL); return -1; }
  const auto service = std::to_string(port);
  addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const int resolve_status = getaddrinfo(name.c_str(), service.c_str(), &hints, &results);
  if (resolve_status != 0) { raz_set_last_error(resolve_status); return -1; }
  Socket connected = invalid_socket;
  std::int64_t connect_error = 0;
  for (auto* current = results; current != nullptr; current = current->ai_next) {
    Socket candidate = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
    if (candidate == invalid_socket) { raz_set_socket_error(); connect_error = raz_last_error_code; continue; }
    if (::connect(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) { connected = candidate; break; }
    raz_set_socket_error();
    connect_error = raz_last_error_code;
    close_socket(candidate);
  }

  freeaddrinfo(results);
  if (connected == invalid_socket) { raz_set_last_error(connect_error); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(connected);
}

std::int64_t raz_rt_tcp_listen(std::int64_t port, std::int64_t backlog) {
#if defined(_WIN32)
  if (!winsock().ok) { raz_set_socket_error(); return -1; }
#endif
  if (port < 0 || port > 65535 || backlog <= 0) { raz_set_last_error(EINVAL); return -1; }
  Socket socket_value = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_value == invalid_socket) { raz_set_socket_error(); return -1; }
  int enabled = 1;
  setsockopt(socket_value, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons(static_cast<unsigned short>(port));
  if (::bind(socket_value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(socket_value, static_cast<int>(backlog)) != 0) {
    raz_set_socket_error();
    close_socket(socket_value);
    return -1;
  }

  raz_clear_last_error();
  return static_cast<std::int64_t>(socket_value);
}

std::int64_t raz_rt_tcp_accept(std::int64_t listener) {
  const Socket accepted = ::accept(static_cast<Socket>(listener), nullptr, nullptr);
  if (accepted == invalid_socket) { raz_set_socket_error(); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(accepted);
}

struct RazPollRecord {
  std::int64_t socket;
  std::int64_t interests;
  std::int64_t events;
};
static_assert(sizeof(RazPollRecord) == 24);

std::int64_t raz_rt_socket_poll_many(RazPollRecord* records, std::int64_t count,
                                      std::int64_t timeout_millis) {
  constexpr std::int64_t max_records = 1048576;
  if (records == nullptr || count <= 0 || count > max_records || timeout_millis < -1) {
    raz_set_last_error(EINVAL);
    return -1;
  }

  const int timeout = timeout_millis < 0 ? -1 : static_cast<int>(std::min<std::int64_t>(timeout_millis, INT_MAX));
#if defined(_WIN32)
  if (!winsock().ok) { raz_set_socket_error(); return -1; }
  std::array<WSAPOLLFD, 256> stack_fds{};
  thread_local std::vector<WSAPOLLFD> dynamic_fds;
  WSAPOLLFD* poll_fds = stack_fds.data();
  if (count > static_cast<std::int64_t>(stack_fds.size())) {
    dynamic_fds.resize(static_cast<std::size_t>(count));
    poll_fds = dynamic_fds.data();
  }
  for (std::int64_t index = 0; index < count; ++index) {
    if (records[index].socket < 0 || (records[index].interests & 3) == 0) {
      raz_set_last_error(EINVAL);
      return -1;
    }
    auto& descriptor = poll_fds[static_cast<std::size_t>(index)];
    descriptor = WSAPOLLFD{};
    descriptor.fd = static_cast<Socket>(records[index].socket);
    if ((records[index].interests & 1) != 0) descriptor.events |= POLLRDNORM;
    if ((records[index].interests & 2) != 0) descriptor.events |= POLLWRNORM;
    records[index].events = 0;
  }
  const int ready = ::WSAPoll(poll_fds, static_cast<ULONG>(count), timeout);
#else
  std::array<pollfd, 256> stack_fds{};
  thread_local std::vector<pollfd> dynamic_fds;
  pollfd* poll_fds = stack_fds.data();
  if (count > static_cast<std::int64_t>(stack_fds.size())) {
    dynamic_fds.resize(static_cast<std::size_t>(count));
    poll_fds = dynamic_fds.data();
  }
  for (std::int64_t index = 0; index < count; ++index) {
    if (records[index].socket < 0 || (records[index].interests & 3) == 0) {
      raz_set_last_error(EINVAL);
      return -1;
    }
    auto& descriptor = poll_fds[static_cast<std::size_t>(index)];
    descriptor = pollfd{};
    descriptor.fd = static_cast<int>(records[index].socket);
    if ((records[index].interests & 1) != 0) descriptor.events |= POLLIN;
    if ((records[index].interests & 2) != 0) descriptor.events |= POLLOUT;
    records[index].events = 0;
  }
  const int ready = ::poll(poll_fds, static_cast<nfds_t>(count), timeout);
#endif
  if (ready < 0) { raz_set_socket_error(); return -1; }

  for (std::int64_t index = 0; index < count; ++index) {
    const short returned = poll_fds[static_cast<std::size_t>(index)].revents;
    std::int64_t events = 0;
#if defined(_WIN32)
    if ((returned & (POLLRDNORM | POLLRDBAND | POLLHUP)) != 0) events |= 1;
    if ((returned & (POLLWRNORM | POLLWRBAND)) != 0) events |= 2;
#else
    if ((returned & (POLLIN | POLLPRI | POLLHUP)) != 0) events |= 1;
    if ((returned & POLLOUT) != 0) events |= 2;
#endif
    if ((returned & (POLLERR | POLLNVAL)) != 0) events |= 4;
    records[index].events = events;
  }
  raz_clear_last_error();
  return static_cast<std::int64_t>(ready);
}

std::int64_t raz_rt_socket_poll(std::int64_t socket_value, std::int64_t interests,
                                 std::int64_t timeout_millis) {
  RazPollRecord record{socket_value, interests, 0};
  const auto ready = raz_rt_socket_poll_many(&record, 1, timeout_millis);
  return ready < 0 ? -1 : record.events;
}

std::int64_t raz_rt_socket_send(std::int64_t socket_value, const void* data, std::int64_t size) {
  if (data == nullptr || size < 0) { raz_set_last_error(EINVAL); return -1; }
#if defined(_WIN32)
  const auto result = static_cast<std::int64_t>(::send(static_cast<Socket>(socket_value), static_cast<const char*>(data), static_cast<int>(size), 0));
#else
  const auto result = static_cast<std::int64_t>(::send(static_cast<Socket>(socket_value), data, static_cast<std::size_t>(size), 0));
#endif
  if (result < 0) { raz_set_socket_error(); return -1; }
  raz_clear_last_error();
  return result;
}

std::int64_t raz_rt_socket_receive(std::int64_t socket_value, void* data, std::int64_t capacity) {
  if (data == nullptr || capacity < 0) { raz_set_last_error(EINVAL); return -1; }
#if defined(_WIN32)
  const auto result = static_cast<std::int64_t>(::recv(static_cast<Socket>(socket_value), static_cast<char*>(data), static_cast<int>(capacity), 0));
#else
  const auto result = static_cast<std::int64_t>(::recv(static_cast<Socket>(socket_value), data, static_cast<std::size_t>(capacity), 0));
#endif
  if (result < 0) { raz_set_socket_error(); return -1; }
  raz_clear_last_error();
  return result;
}

struct RazIoSliceRecord {
  std::uintptr_t data;
  std::int64_t length;
};
static_assert(sizeof(RazIoSliceRecord) == 16);

static bool raz_validate_io_slices(const RazIoSliceRecord* slices, std::int64_t count) {
  if (slices == nullptr || count <= 0 || count > 64) return false;
  for (std::int64_t index = 0; index < count; ++index) {
    if (slices[index].length < 0 || (slices[index].length > 0 && slices[index].data == 0)) return false;
#if defined(_WIN32)
    if (slices[index].length > static_cast<std::int64_t>(std::numeric_limits<ULONG>::max())) return false;
#endif
  }
  return true;
}

std::int64_t raz_rt_socket_send_vectored(std::int64_t socket_value, const RazIoSliceRecord* slices, std::int64_t count) {
  if (!raz_validate_io_slices(slices, count)) { raz_set_last_error(EINVAL); return -1; }
#if defined(_WIN32)
  WSABUF buffers[64]{};
  for (std::int64_t index = 0; index < count; ++index) {
    buffers[index].buf = reinterpret_cast<char*>(slices[index].data);
    buffers[index].len = static_cast<ULONG>(slices[index].length);
  }
  DWORD sent = 0;
  if (WSASend(static_cast<Socket>(socket_value), buffers, static_cast<DWORD>(count), &sent, 0, nullptr, nullptr) != 0) {
    raz_set_socket_error();
    return -1;
  }

  raz_clear_last_error();
  return static_cast<std::int64_t>(sent);
#else
  const auto result = ::writev(static_cast<Socket>(socket_value), reinterpret_cast<const iovec*>(slices), static_cast<int>(count));
  if (result < 0) { raz_set_socket_error(); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(result);
#endif
}

std::int64_t raz_rt_socket_receive_vectored(std::int64_t socket_value, const RazIoSliceRecord* slices, std::int64_t count) {
  if (!raz_validate_io_slices(slices, count)) { raz_set_last_error(EINVAL); return -1; }
#if defined(_WIN32)
  WSABUF buffers[64]{};
  for (std::int64_t index = 0; index < count; ++index) {
    buffers[index].buf = reinterpret_cast<char*>(slices[index].data);
    buffers[index].len = static_cast<ULONG>(slices[index].length);
  }
  DWORD received = 0;
  DWORD flags = 0;
  if (WSARecv(static_cast<Socket>(socket_value), buffers, static_cast<DWORD>(count), &received, &flags, nullptr, nullptr) != 0) {
    raz_set_socket_error();
    return -1;
  }

  raz_clear_last_error();
  return static_cast<std::int64_t>(received);
#else
  const auto result = ::readv(static_cast<Socket>(socket_value), reinterpret_cast<const iovec*>(slices), static_cast<int>(count));
  if (result < 0) { raz_set_socket_error(); return -1; }
  raz_clear_last_error();
  return static_cast<std::int64_t>(result);
#endif
}

void raz_rt_socket_close(std::int64_t socket_value) { close_socket(static_cast<Socket>(socket_value)); }


}
