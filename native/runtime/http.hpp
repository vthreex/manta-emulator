// Minimal HTTP/1.1 client for the TV browser (Linux only): URL parsing and resolution, GET over TCP with redirects, chunked transfer decoding.
// HTTPS is added on top of the same connection interface later (see docs/browser.md).
#pragma once
#include <map>
#include <string>

namespace web {

struct Url {
  std::string scheme = "http", host, port, path = "/";   // path includes the query
  std::string str() const;
};

struct Response {
  int status = 0;
  std::map<std::string, std::string> headers;   // lower-case names
  std::string body;
  std::string finalUrl;
  std::string error;                            // non-empty: the request failed (message for the user)
  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

bool parseUrl(const std::string& text, Url& out);
std::string resolveUrl(const std::string& base, const std::string& ref);   // absolute URL for a reference found on page `base`
Response get(const std::string& url, int maxRedirects = 6, size_t maxBody = 4u << 20, int timeoutMs = 15000);

}  // namespace web
