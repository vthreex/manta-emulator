// Minimal HTTP/1.1 client for the TV browser (Linux only): URL parsing and resolution, GET over TCP with redirects, chunked transfer decoding.
// HTTPS is added on top of the same connection interface later (see docs/browser.md).
#pragma once
#include <map>
#include <string>
#include <vector>

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
  std::string charset;                          // from Content-Type, lower case ("" if absent)
  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

bool parseUrl(const std::string& text, Url& out);
std::string resolveUrl(const std::string& base, const std::string& ref);   // absolute URL for a reference found on page `base`
struct Request {
  std::string method = "GET";
  std::string url;
  std::string body;                             // for POST
  std::string contentType = "application/x-www-form-urlencoded";
};
// Performs the request, follows redirects (301/302/303 become GET), keeps an in-memory cookie jar for the whole session.
Response fetch(const Request& req, int maxRedirects = 6, size_t maxBody = 4u << 20, int timeoutMs = 15000);
Response get(const std::string& url, int maxRedirects = 6, size_t maxBody = 4u << 20, int timeoutMs = 15000);
std::string urlEncode(const std::string& s);                                  // application/x-www-form-urlencoded escaping

}  // namespace web
