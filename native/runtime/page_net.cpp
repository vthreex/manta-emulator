// The network page: connection status (read from the file the boot-time network helper writes, /run/net.txt) and a ping tool. Like the keyboard it is a page of the
// original firmware ZUI (see pages.hpp). Pinging runs on a worker thread (raw ICMP socket, native/runtime/net_ping.h); the page picks the lines up between runs.
#include "pages.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef __linux__
#include "net_ping.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#endif

namespace pages {
namespace {

constexpr unsigned kResultLines = 7, kStatusLines = 4;

std::u16string widen(const std::string& s) { std::u16string o; for (char ch : s) o.push_back(char16_t(uint8_t(ch))); return o; }

#ifdef __linux__
struct Job {
  std::mutex m;
  std::vector<std::string> lines;
  std::atomic<uint64_t> version{0};
  std::atomic<bool> running{false};
  void add(const std::string& s) { { std::lock_guard<std::mutex> l(m); lines.push_back(s); if (lines.size() > 200) lines.erase(lines.begin()); } ++version; }
};

std::map<std::string, std::string> readStatus() {
  std::map<std::string, std::string> kv;
  std::ifstream f("/run/net.txt");
  std::string line;
  while (std::getline(f, line)) { auto e = line.find('='); if (e != std::string::npos) kv[line.substr(0, e)] = line.substr(e + 1); }
  return kv;
}

void pingWorker(std::shared_ptr<Job> job, std::string host) {
  job->running = true;
  job->add("PING " + host);
  struct in_addr addr;
  int rc = ping_resolve(host.c_str(), &addr);
  if (rc != 0) { job->add("cannot resolve '" + host + "'"); job->running = false; return; }
  char ip[48]; inet_ntop(AF_INET, &addr, ip, sizeof ip);
  if (host != ip) job->add(std::string("resolved to ") + ip);
  int received = 0; double total = 0;
  for (int seq = 1; seq <= 4; ++seq) {
    struct ping_result r;
    ping_once(addr, 0x4d41, seq, 2000, &r);
    char b[160];
    if (r.ok) { ++received; total += r.ms; std::snprintf(b, sizeof b, "reply from %s: seq=%d time=%.1f ms ttl=%d", ip, seq, r.ms, r.ttl); }
    else if (r.error) std::snprintf(b, sizeof b, "seq=%d: send failed (%s)", seq, std::strerror(r.error));
    else std::snprintf(b, sizeof b, "seq=%d: request timed out", seq);
    job->add(b);
    if (seq < 4) usleep(600000);
  }
  char s[160];
  if (received) std::snprintf(s, sizeof s, "4 sent, %d received, average %.1f ms", received, total / received);
  else std::snprintf(s, sizeof s, "4 sent, 0 received");
  job->add(s);
  job->running = false;
}
#endif

struct NetState {
  std::string lastHost = "example.com";
  std::string pending;                     // ping target entered on the keyboard, started by the next service tick
  std::string gateway;
  unsigned firstResult = 0, firstStatus = 0, titleHwnd = 0;
  uint64_t lastStatusAt = 0;
  uint64_t seenVersion = ~0ull;
#ifdef __linux__
  std::shared_ptr<Job> job = std::make_shared<Job>();
#endif
};

void refreshStatus(Host& host, NetState& ns) {
  std::vector<std::string> l(kStatusLines);
#ifdef __linux__
  auto kv = readStatus();
  const std::string state = kv.count("state") ? kv["state"] : "unknown";
  ns.gateway = kv.count("gateway") ? kv["gateway"] : "";
  if (state == "up") {
    l[0] = "Interface: " + kv["iface"] + "    State: connected";
    l[1] = "Address: " + kv["ip"] + "    Mask: " + kv["mask"];
    l[2] = "Gateway: " + kv["gateway"] + "    DNS: " + kv["dns"];
    l[3] = "MAC: " + kv["mac"];
  } else {
    l[0] = "State: " + (state == "no-interface" ? std::string("no network interface found") : state == "no-dhcp" ? std::string("no DHCP answer") : state);
    if (kv.count("iface")) l[1] = "Interface: " + kv["iface"];
    if (kv.count("mac")) l[2] = "MAC: " + kv["mac"];
  }
#else
  l[0] = "Network is not available in this build";
#endif
  for (unsigned i = 0; i < kStatusLines; ++i) host.setLabel(ns.firstStatus + i, widen(l[i]));
}

#ifdef __linux__
void showResults(Host& host, NetState& ns) {
  std::vector<std::string> copy;
  { std::lock_guard<std::mutex> lk(ns.job->m); copy = ns.job->lines; }
  const size_t n = copy.size();
  for (unsigned i = 0; i < kResultLines; ++i) {
    size_t idx = n > kResultLines ? n - kResultLines + i : i;
    host.setLabel(ns.firstResult + i, widen(idx < n ? copy[idx] : std::string()));
  }
  ns.seenVersion = ns.job->version.load();
}
void startPing(Host& host, NetState& ns, const std::string& target) {
  if (ns.job->running) return;
  { std::lock_guard<std::mutex> lk(ns.job->m); ns.job->lines.clear(); }
  ++ns.job->version;
  std::thread(pingWorker, ns.job, target).detach();
  (void)host;
}
#endif

}  // namespace

void requestNetworkPage(Host& host) {
  static PagePtr page;
  static std::shared_ptr<NetState> ns;
  if (page) { page->focus = 0; host.request(page); return; }
  ns = std::make_shared<NetState>();
  auto pg = std::make_shared<PageDef>();
  page = pg;
  pg->name = "network";
  constexpr int pageW = 780, pageH = 566, x0 = 20;
  Win root; root.cls = 0; root.flags = 0x81; root.w = pageW; root.h = pageH; root.x = uint16_t((1360 - pageW) / 2); root.y = uint16_t(768 - pageH - 30);
  pg->add(root);
  Win panel; panel.w = pageW; panel.h = pageH; panel.normal = {kPanelTeal};
  pg->add(panel);
  auto text = [&](int x, int y, int w, int h, unsigned style, bool dark) {
    Win t; t.cls = 3; t.x = uint16_t(x); t.y = uint16_t(y); t.w = uint16_t(w); t.h = uint16_t(h);
    if (dark) t.normal = {kFillBlack, comp(3, style)}; else t.normal = {comp(3, style)};
    t.focus = t.normal; t.disabled = t.normal;
    return t;
  };
  ns->titleHwnd = pg->add(text(x0, 10, pageW - 2 * x0, 40, kTextCentre, false), u"Network");
  ns->firstStatus = unsigned(pg->windows.size());
  for (unsigned i = 0; i < kStatusLines; ++i) pg->add(text(x0, 54 + 36 * int(i), pageW - 2 * x0, 36, kTextLeft, false), u" ");
  ns->firstResult = unsigned(pg->windows.size());
  for (unsigned i = 0; i < kResultLines; ++i) pg->add(text(x0, 208 + 36 * int(i), pageW - 2 * x0, 36, kTextLeft, true), i == 0 ? u"Ping results appear here" : u" ");
  const char16_t* names[] = {u"Ping host...", u"Ping gateway", u"Refresh", u"Close"};
  for (unsigned i = 0; i < 4; ++i) {
    Win b; b.cls = 3; b.x = uint16_t(x0 + int(i) * 190); b.y = uint16_t(pageH - 64); b.w = 174; b.h = 44;
    b.normal = {kFillDark, comp(3, kTextCentre)}; b.focus = {kFillHighlight, comp(3, kTextCentreDark)}; b.disabled = {kFillDark, comp(3, kTextCentreGrey)};
    Item it; it.row = 0; it.cx = b.x + b.w / 2;
    it.hwnd = pg->add(b, names[i]);
    pg->items.push_back(it);
  }
  NetState* s = ns.get();
  std::weak_ptr<PageDef> self = pg;
  pg->onPress = [s, self](Host& host, unsigned item) {
    if (item == 0) {
      auto me = self.lock();
      requestKeyboard(host, s->lastHost, [s](bool accepted, const std::string& t) {
#ifdef __linux__
        if (accepted && !t.empty()) { s->lastHost = t; s->pending = t; }
#else
        (void)accepted; (void)t; (void)s;
#endif
      }, me);
    } else if (item == 1) {
#ifdef __linux__
      if (!s->gateway.empty()) startPing(host, *s, s->gateway); else s->job->add("no gateway: network is not up");
#endif
    } else if (item == 2) refreshStatus(host, *s);
    else host.closeAll();
  };
  pg->onService = [s, self](Host& host) {
#ifdef __linux__
    if (!s->pending.empty()) { std::string t = s->pending; s->pending.clear(); startPing(host, *s, t); }
    if (s->job->version.load() != s->seenVersion) showResults(host, *s);
#endif
    if (host.dispatches() > s->lastStatusAt + 40000000 || s->lastStatusAt == 0) { s->lastStatusAt = host.dispatches() ? host.dispatches() : 1; refreshStatus(host, *s); }
  };
  pg->onClosed = [s](Host&) { s->lastStatusAt = 0; s->seenVersion = ~0ull; };
  refreshStatus(host, *s);
  host.request(pg);
}

}  // namespace pages
