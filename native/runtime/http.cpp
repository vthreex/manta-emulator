#include "http.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef __linux__
#if defined(MANTA_TLS)
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <mutex>
#endif
#include <memory>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace web {

std::string Url::str() const {
  std::string s = scheme + "://" + host;
  if (!port.empty() && !((scheme == "http" && port == "80") || (scheme == "https" && port == "443"))) s += ":" + port;
  return s + path;
}

static std::string lower(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }

bool parseUrl(const std::string& text, Url& out) {
  std::string t = text;
  size_t b = t.find_first_not_of(" \t\r\n"), e = t.find_last_not_of(" \t\r\n");
  if (b == std::string::npos) return false;
  t = t.substr(b, e - b + 1);
  Url u;
  size_t p = t.find("://");
  if (p == std::string::npos) t = "http://" + t, p = 4;
  u.scheme = lower(t.substr(0, p));
  if (u.scheme != "http" && u.scheme != "https") return false;
  std::string rest = t.substr(p + 3);
  size_t slash = rest.find_first_of("/?#");
  std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string tail = slash == std::string::npos ? "/" : rest.substr(slash);
  size_t hash = tail.find('#');
  if (hash != std::string::npos) tail = tail.substr(0, hash);
  if (tail.empty() || tail[0] == '?') tail = "/" + tail;
  size_t at = hostport.rfind('@');
  if (at != std::string::npos) hostport = hostport.substr(at + 1);
  size_t colon = hostport.rfind(':');
  if (colon != std::string::npos && hostport.find(']') == std::string::npos) { u.port = hostport.substr(colon + 1); hostport = hostport.substr(0, colon); }
  if (hostport.empty()) return false;
  u.host = lower(hostport);
  if (u.port.empty()) u.port = u.scheme == "https" ? "443" : "80";
  u.path = tail;
  out = u;
  return true;
}

static std::string removeDotSegments(const std::string& path) {
  std::string query;
  std::string p = path;
  size_t q = p.find('?');
  if (q != std::string::npos) { query = p.substr(q); p = p.substr(0, q); }
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= p.size()) {
    size_t j = p.find('/', i);
    std::string seg = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
    if (seg == "..") { if (!out.empty()) out.pop_back(); }
    else if (seg != "." ) out.push_back(seg);
    if (j == std::string::npos) break;
    i = j + 1;
  }
  std::string r;
  for (size_t k = 0; k < out.size(); ++k) { if (k) r += "/"; r += out[k]; }
  if (r.empty() || r[0] != '/') r = "/" + r;
  return r + query;
}

std::string resolveUrl(const std::string& base, const std::string& ref0) {
  std::string ref = ref0;
  size_t b = ref.find_first_not_of(" \t\r\n"), e = ref.find_last_not_of(" \t\r\n");
  if (b == std::string::npos) return base;
  ref = ref.substr(b, e - b + 1);
  std::string lref = lower(ref);
  if (lref.rfind("http://", 0) == 0 || lref.rfind("https://", 0) == 0) { Url u; return parseUrl(ref, u) ? u.str() : ref; }
  Url bu;
  if (!parseUrl(base, bu)) return ref;
  if (ref.rfind("//", 0) == 0) { Url u; return parseUrl(bu.scheme + ":" + ref, u) ? u.str() : ref; }
  if (lref.rfind("javascript:", 0) == 0 || lref.rfind("mailto:", 0) == 0 || lref.rfind("tel:", 0) == 0 || ref[0] == '#') return std::string();   // not navigable here
  Url r = bu;
  if (ref[0] == '/') r.path = removeDotSegments(ref);
  else if (ref[0] == '?') { std::string pth = bu.path; size_t q = pth.find('?'); if (q != std::string::npos) pth = pth.substr(0, q); r.path = pth + ref; }
  else {
    std::string pth = bu.path; size_t q = pth.find('?'); if (q != std::string::npos) pth = pth.substr(0, q);
    size_t s = pth.rfind('/');
    r.path = removeDotSegments((s == std::string::npos ? std::string("/") : pth.substr(0, s + 1)) + ref);
  }
  size_t hash = r.path.find('#');
  if (hash != std::string::npos) r.path = r.path.substr(0, hash);
  return r.str();
}

#ifdef __linux__

namespace {

struct Conn {
  int fd = -1;
  ~Conn() { if (fd >= 0) close(fd); }
};

bool waitFd(int fd, short events, int timeoutMs) {
  struct pollfd p = {fd, events, 0};
  int r;
  do { r = poll(&p, 1, timeoutMs); } while (r < 0 && errno == EINTR);
  return r > 0;
}

std::string connectTo(Conn& c, const Url& u, int timeoutMs) {
  struct addrinfo hints, *res = nullptr;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  int rc = getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res);
  if (rc != 0 || !res) return "cannot resolve " + u.host;
  std::string err = "cannot connect to " + u.host;
  for (struct addrinfo* a = res; a; a = a->ai_next) {
    int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (fd < 0) continue;
    int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = connect(fd, a->ai_addr, a->ai_addrlen);
    if (r < 0 && errno == EINPROGRESS && waitFd(fd, POLLOUT, timeoutMs)) {
      int so = 0; socklen_t sl = sizeof so; getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl);
      r = so == 0 ? 0 : -1;
    }
    if (r == 0) { fcntl(fd, F_SETFL, fl); c.fd = fd; freeaddrinfo(res); return std::string(); }
    close(fd);
  }
  freeaddrinfo(res);
  return err;
}

bool sendAll(int fd, const std::string& s, int timeoutMs) {
  size_t off = 0;
  while (off < s.size()) {
    if (!waitFd(fd, POLLOUT, timeoutMs)) return false;
    ssize_t n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
    if (n <= 0) { if (errno == EINTR || errno == EAGAIN) continue; return false; }
    off += size_t(n);
  }
  return true;
}

// Reads everything until the peer closes (Connection: close) or the size limit is hit.
std::string readAllFdImpl(int fd, size_t maxBytes, int timeoutMs, std::string& err) {
  std::string data;
  char buf[16384];
  for (;;) {
    if (!waitFd(fd, POLLIN, timeoutMs)) { err = "timed out"; break; }
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n == 0) break;
    if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; err = std::strerror(errno); break; }
    data.append(buf, size_t(n));
    if (data.size() > maxBytes + 65536) { err = "page too large"; break; }
  }
  return data;
}

std::string dechunk(const std::string& in) {
  std::string out;
  size_t pos = 0;
  while (pos < in.size()) {
    size_t eol = in.find("\r\n", pos);
    if (eol == std::string::npos) break;
    size_t size = std::strtoul(in.substr(pos, eol - pos).c_str(), nullptr, 16);
    pos = eol + 2;
    if (size == 0) break;
    if (pos + size > in.size()) { out.append(in, pos, std::string::npos); break; }
    out.append(in, pos, size);
    pos += size + 2;
  }
  return out;
}

// A connection the request goes over: plain TCP or TLS.
struct Transport {
  virtual ~Transport() {}
  virtual bool send(const std::string& data, int timeoutMs) = 0;
  virtual std::string readAll(size_t maxBytes, int timeoutMs, std::string& err) = 0;
};

struct PlainTransport : Transport {
  Conn& c;
  explicit PlainTransport(Conn& conn) : c(conn) {}
  bool send(const std::string& data, int timeoutMs) override { return sendAll(c.fd, data, timeoutMs); }
  std::string readAll(size_t maxBytes, int timeoutMs, std::string& err) override { return readAllFdImpl(c.fd, maxBytes, timeoutMs, err); }
};

#if defined(MANTA_TLS)
std::mutex g_tlsMutex;
mbedtls_x509_crt g_ca;
bool g_tlsInit = false, g_caOk = false;
int g_caCerts = 0;

std::string tlsError(int rc) { char b[160]; mbedtls_strerror(rc, b, sizeof b); return b; }

struct TlsTransport : Transport {
  Conn& c;
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_entropy_context entropy;
  bool up = false;

  explicit TlsTransport(Conn& conn) : c(conn) {
    mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf); mbedtls_ctr_drbg_init(&drbg); mbedtls_entropy_init(&entropy);
    net.fd = c.fd;
  }
  ~TlsTransport() override {
    if (up) mbedtls_ssl_close_notify(&ssl);
    net.fd = -1;   // the socket belongs to Conn
    mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy);
  }
  // returns an error text, empty on success
  std::string handshake(const std::string& host, int timeoutMs) {
    {
      std::lock_guard<std::mutex> lk(g_tlsMutex);
      if (!g_tlsInit) {
        g_tlsInit = true;
        psa_crypto_init();
        mbedtls_x509_crt_init(&g_ca);
        int n = mbedtls_x509_crt_parse_file(&g_ca, "/etc/ssl/cacert.pem");
        g_caOk = n >= 0;
        g_caCerts = 0;
        for (const mbedtls_x509_crt* p = &g_ca; p && p->raw.p; p = p->next) ++g_caCerts;
      }
      if (!g_caOk || g_caCerts == 0) return "no certificate authorities installed (/etc/ssl/cacert.pem)";
    }
    int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
    if (rc) return "random generator: " + tlsError(rc);
    rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) return "tls config: " + tlsError(rc);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &g_ca, nullptr);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_read_timeout(&conf, uint32_t(timeoutMs));
    if ((rc = mbedtls_ssl_setup(&ssl, &conf)) != 0) return "tls setup: " + tlsError(rc);
    if ((rc = mbedtls_ssl_set_hostname(&ssl, host.c_str())) != 0) return "tls hostname: " + tlsError(rc);
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
    for (;;) {
      rc = mbedtls_ssl_handshake(&ssl);
      if (rc == 0) break;
      if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        char info[300];
        uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
        mbedtls_x509_crt_verify_info(info, sizeof info, "", flags);
        std::string t = info;
        while (!t.empty() && (t.back() == char(10) || t.back() == ' ')) t.pop_back();
        for (char& ch : t) if (ch == char(10)) ch = ';';
        return "certificate not trusted: " + t;
      }
      return "secure connection failed: " + tlsError(rc);
    }
    up = true;
    return std::string();
  }
  bool send(const std::string& data, int) override {
    size_t off = 0;
    while (off < data.size()) {
      int n = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(data.data()) + off, data.size() - off);
      if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      if (n <= 0) return false;
      off += size_t(n);
    }
    return true;
  }
  std::string readAll(size_t maxBytes, int, std::string& err) override {
    std::string data;
    unsigned char buf[16384];
    for (;;) {
      int n = mbedtls_ssl_read(&ssl, buf, sizeof buf);
      if (n > 0) { data.append(reinterpret_cast<char*>(buf), size_t(n)); if (data.size() > maxBytes + 65536) { err = "page too large"; break; } continue; }
      if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
      if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
      if (n == MBEDTLS_ERR_SSL_TIMEOUT) { err = "timed out"; break; }
      if (data.empty()) err = "secure read failed: " + tlsError(n);
      break;   // abrupt close after some data: use what we got
    }
    return data;
  }
};
#endif

}  // namespace

Response get(const std::string& url0, int maxRedirects, size_t maxBody, int timeoutMs) {
  Response r;
  std::string url = url0;
  for (int hop = 0;; ++hop) {
    Url u;
    if (!parseUrl(url, u)) { r.error = "bad address: " + url; return r; }
    r.finalUrl = u.str();
    Conn c;
    std::string e = connectTo(c, u, timeoutMs);
    if (!e.empty()) { r.error = e; return r; }
    std::unique_ptr<Transport> t;
    if (u.scheme == "https") {
#if defined(MANTA_TLS)
      auto tls = std::make_unique<TlsTransport>(c);
      std::string he = tls->handshake(u.host, timeoutMs);
      if (!he.empty()) { r.error = he; return r; }
      t = std::move(tls);
#else
      r.error = "HTTPS is not available in this build";
      return r;
#endif
    } else t = std::make_unique<PlainTransport>(c);
    std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + (u.port != "80" ? ":" + u.port : std::string()) +
                      "\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64) MantaTV/1.0\r\nAccept: text/html,text/plain;q=0.9,*/*;q=0.5\r\nAccept-Language: en\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    if (!t->send(req, timeoutMs)) { r.error = "send failed"; return r; }
    std::string err;
    std::string raw = t->readAll(maxBody, timeoutMs, err);
    size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) { r.error = err.empty() ? "no response" : err; return r; }
    std::string head = raw.substr(0, hdrEnd);
    std::string body = raw.substr(hdrEnd + 4);
    r.headers.clear();
    size_t eol = head.find("\r\n");
    std::string statusLine = head.substr(0, eol);
    size_t sp = statusLine.find(' ');
    r.status = sp == std::string::npos ? 0 : std::atoi(statusLine.c_str() + sp + 1);
    while (eol != std::string::npos) {
      size_t next = head.find("\r\n", eol + 2);
      std::string line = head.substr(eol + 2, next == std::string::npos ? std::string::npos : next - eol - 2);
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string v = line.substr(colon + 1);
        size_t vb = v.find_first_not_of(" \t");
        r.headers[lower(line.substr(0, colon))] = vb == std::string::npos ? std::string() : v.substr(vb);
      }
      eol = next;
    }
    if (lower(r.headers["transfer-encoding"]).find("chunked") != std::string::npos) body = dechunk(body);
    else if (r.headers.count("content-length")) { size_t n = std::strtoul(r.headers["content-length"].c_str(), nullptr, 10); if (n < body.size()) body.resize(n); }
    if (r.status >= 300 && r.status < 400 && r.headers.count("location") && hop < maxRedirects) {
      std::string next = resolveUrl(r.finalUrl, r.headers["location"]);
      if (!next.empty()) { url = next; continue; }
    }
    if (body.size() > maxBody) body.resize(maxBody);
    r.body = std::move(body);
    if (!err.empty() && r.body.empty()) r.error = err;
    return r;
  }
}

#else

Response get(const std::string& url, int, size_t, int) { Response r; r.finalUrl = url; r.error = "networking is only available in the Linux build"; return r; }

#endif

}  // namespace web
