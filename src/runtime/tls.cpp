// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "runtime_internal.hpp"

using namespace raz::runtime_detail;

extern "C" {
std::int64_t raz_rt_last_error_code() { return raz_last_error_code; }

std::int64_t raz_rt_tls_available() {
#if defined(RAZ_HAVE_OPENSSL)
  return raz_tls_client_context() != nullptr ? 1 : 0;
#else
  return 0;
#endif
}

void* raz_rt_tls_client_create(const char* host, std::int64_t host_length) {
#if defined(RAZ_HAVE_OPENSSL)
  raz_clear_last_error();
  if (host == nullptr || host_length <= 0) { raz_set_last_error(-1); return nullptr; }
  auto* context = raz_tls_client_context();
  if (context == nullptr) { raz_tls_set_error(); return nullptr; }
  std::string hostname(host, static_cast<std::size_t>(host_length));
  SSL* ssl = SSL_new(context);
  if (ssl == nullptr) { raz_tls_set_error(); return nullptr; }
  BIO* input = BIO_new(BIO_s_mem());
  BIO* output = BIO_new(BIO_s_mem());
  if (input == nullptr || output == nullptr) {
    if (input != nullptr) BIO_free(input);
    if (output != nullptr) BIO_free(output);
    SSL_free(ssl);
    raz_tls_set_error();
    return nullptr;
  }

  BIO_set_mem_eof_return(input, -1);
  BIO_set_mem_eof_return(output, -1);
  SSL_set_bio(ssl, input, output); // SSL owns both BIOs from here.
  SSL_set_connect_state(ssl);

  if (SSL_SESSION* cached = raz_tls_cached_session(hostname); cached != nullptr) {
    // SSL_set_session takes its own reference on success.
    (void)SSL_set_session(ssl, cached);
    SSL_SESSION_free(cached);
  }

  unsigned char address[16]{};
  X509_VERIFY_PARAM* parameter = SSL_get0_param(ssl);
  bool is_ip = inet_pton(AF_INET, hostname.c_str(), address) == 1 || inet_pton(AF_INET6, hostname.c_str(), address) == 1;
  if (is_ip) {
    if (X509_VERIFY_PARAM_set1_ip_asc(parameter, hostname.c_str()) != 1) {
      SSL_free(ssl); raz_tls_set_error(); return nullptr;
    }
  } else {
    if (SSL_set1_host(ssl, hostname.c_str()) != 1 || SSL_set_tlsext_host_name(ssl, hostname.c_str()) != 1) {
      SSL_free(ssl); raz_tls_set_error(); return nullptr;
    }
  }
  try { return new RazTlsSession{ssl, std::move(hostname)}; }
  catch (...) { SSL_free(ssl); raz_set_last_error(-1); return nullptr; }
#else
  (void)host; (void)host_length; raz_set_last_error(-1); return nullptr;
#endif
}

std::int64_t raz_rt_tls_feed(void* handle, const void* data, std::int64_t size) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr || session->ssl == nullptr || size < 0 || (size > 0 && data == nullptr)) return -1;
  if (size == 0) return 0;
  const int written = BIO_write(SSL_get_rbio(session->ssl), data, static_cast<int>(std::min<std::int64_t>(size, std::numeric_limits<int>::max())));
  if (written <= 0) { raz_tls_set_error(); return -1; }
  return written;
#else
  (void)handle; (void)data; (void)size; return -1;
#endif
}

std::int64_t raz_rt_tls_drain(void* handle, void* output, std::int64_t capacity) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr || session->ssl == nullptr || capacity < 0 || (capacity > 0 && output == nullptr)) return -1;
  if (capacity == 0) return 0;
  BIO* bio = SSL_get_wbio(session->ssl);
  const int count = BIO_read(bio, output, static_cast<int>(std::min<std::int64_t>(capacity, std::numeric_limits<int>::max())));
  if (count > 0) return count;
  if (BIO_should_retry(bio)) return 0;
  return 0;
#else
  (void)handle; (void)output; (void)capacity; return -1;
#endif
}

std::int64_t raz_rt_tls_pending_encrypted(void* handle) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr || session->ssl == nullptr) return -1;
  return static_cast<std::int64_t>(BIO_ctrl_pending(SSL_get_wbio(session->ssl)));
#else
  (void)handle; return -1;
#endif
}

std::int64_t raz_rt_tls_handshake(void* handle) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr || session->ssl == nullptr) return -1;
  const auto state = raz_tls_result(session->ssl, SSL_do_handshake(session->ssl));
  if (state == 1) raz_tls_cache_session(session->hostname, session->ssl);
  return state;
#else
  (void)handle; return -1;
#endif
}

std::int64_t raz_rt_tls_handshake_finished(void* handle) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  return session != nullptr && session->ssl != nullptr && SSL_is_init_finished(session->ssl) == 1 ? 1 : 0;
#else
  (void)handle; return 0;
#endif
}

std::int64_t raz_rt_tls_write_plain(void* handle, const void* data, std::int64_t size) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr || session->ssl == nullptr || size < 0 || (size > 0 && data == nullptr)) return -1;
  if (size == 0) return 0;
  std::size_t written = 0;
  const int result = SSL_write_ex(session->ssl, data, static_cast<std::size_t>(size), &written);
  if (result == 1) return static_cast<std::int64_t>(written);
  const auto state = raz_tls_result(session->ssl, result);
  return state == 0 ? 0 : -1;
#else
  (void)handle; (void)data; (void)size; return -1;
#endif
}

std::int64_t raz_rt_tls_read_plain(void* handle, void* output, std::int64_t capacity) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr || session->ssl == nullptr || capacity < 0 || (capacity > 0 && output == nullptr)) return -1;
  if (capacity == 0) return 0;
  std::size_t read = 0;
  const int result = SSL_read_ex(session->ssl, output, static_cast<std::size_t>(capacity), &read);
  if (result == 1) return static_cast<std::int64_t>(read);
  const auto state = raz_tls_result(session->ssl, result);
  if (state == 2) return -2; // clean TLS close_notify
  return state == 0 ? 0 : -1;
#else
  (void)handle; (void)output; (void)capacity; return -1;
#endif
}

std::int64_t raz_rt_tls_shutdown(void* handle) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr || session->ssl == nullptr) return -1;
  const int result = SSL_shutdown(session->ssl);
  if (result == 1) return 1;
  if (result == 0) return 0;
  return raz_tls_result(session->ssl, result);
#else
  (void)handle; return -1;
#endif
}

void raz_rt_tls_destroy(void* handle) {
#if defined(RAZ_HAVE_OPENSSL)
  auto* session = static_cast<RazTlsSession*>(handle);
  if (session == nullptr) return;
  if (session->ssl != nullptr) {
    // TLS 1.3 tickets commonly arrive after the handshake. Capturing again at
    // teardown preserves the newest resumable session without another ABI.
    raz_tls_cache_session(session->hostname, session->ssl);
    SSL_free(session->ssl);
  }
  delete session;
#else
  (void)handle;
#endif
}


}
