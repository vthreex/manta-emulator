// The browser page: address bar, scrolling text view with a highlight cursor, numbered links, Back. A page of the original firmware ZUI like the keyboard and the
// network page (see pages.hpp); fetching runs on a worker thread (http.cpp), rendering is html_text.cpp.
#include "html_text.hpp"
#include "http.hpp"
#include "pages.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef __linux__
#include <atomic>
#include <mutex>
#include <thread>
#endif

namespace pages {
namespace {

constexpr unsigned kVisible = 16;                     // content lines on screen
constexpr int kContentWidthPx = 1420;                 // wrap width for the content lines
constexpr unsigned kToolbarItems = 3;                 // Back, Address, Close (items 0..2); content lines are items 3..

std::u16string widen(const std::string& s) { std::u16string o; for (char ch : s) o.push_back(char16_t((unsigned char)ch >= 32 && (unsigned char)ch < 127 ? ch : '?')); return o; }

const char* kHomeHtml =
    "<html><head><title>Home</title></head><body><h1>TV browser</h1>"
    "<p>Arrow keys move the highlight, OK follows the selected link (LEFT/RIGHT chooses between several links on a line), Back goes back. "
    "Select the address bar and press OK to type an address.</p>"
    "<h2>Pages to try</h2><ul>"
    "<li><a href=\"http://info.cern.ch/\">info.cern.ch</a> - the first web site (plain HTTP)</li>"
    "<li><a href=\"https://example.com/\">example.com</a> - the classic example page (HTTPS)</li>"
    "<li><a href=\"https://en.wikipedia.org/wiki/Special:Random\">Wikipedia</a> - a random article</li>"
    "<li><a href=\"https://text.npr.org/\">text.npr.org</a> - news as text</li>"
    "<li><a href=\"https://lite.cnn.com/\">lite.cnn.com</a> - news as text</li>"
    "<li><a href=\"http://textfiles.com/\">textfiles.com</a> - old text files</li>"
    "</ul></body></html>";

struct Fetch {
#ifdef __linux__
  std::mutex m;
  bool done = false;
  uint64_t id = 0;
  web::Response response;
  std::atomic<bool> running{false};
#endif
};

struct Browser {
  std::vector<std::string> history;
  int histPos = -1;
  std::string url;
  web::Rendered page;
  std::string status = "Ready";
  size_t scroll = 0;
  int curLink = -1;                    // link selected on the highlighted line
  unsigned lastFocus = ~0u;
  std::string pendingNavigate;         // entered on the keyboard, started by the next service tick
  bool loading = false;
  uint64_t loadId = 0;
  unsigned statusHwnd = 0, addressHwnd = 0, firstLine = 0;
  std::vector<std::u16string> shown;   // what the content windows currently show
  std::shared_ptr<Fetch> fetch = std::make_shared<Fetch>();
  PagePtr self;
};

unsigned focusedLine(const Browser& b, const PageDef& pg) { return pg.focus >= kToolbarItems ? pg.focus - kToolbarItems : ~0u; }

void showLines(Host& host, Browser& b) {
  for (unsigned i = 0; i < kVisible; ++i) {
    size_t idx = b.scroll + i;
    std::u16string t = idx < b.page.lines.size() ? widen(b.page.lines[idx].text) : u" ";
    if (t.empty()) t = u" ";
    if (b.shown.size() <= i) b.shown.resize(kVisible, u"\x01");
    if (b.shown[i] != t) { b.shown[i] = t; host.setLabel(b.firstLine + i, t); }
  }
}

std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 3) + "..."; }

void updateStatus(Host& host, Browser& b, PageDef& pg) {
  std::string s;
  unsigned li = focusedLine(b, pg);
  if (b.loading) s = "Loading " + clip(b.url, 100) + " ...";
  else if (li != ~0u && b.curLink >= 0 && b.curLink < int(b.page.links.size())) s = "[" + std::to_string(b.curLink + 1) + "] " + clip(b.page.links[size_t(b.curLink)], 110);
  else if (!b.page.lines.empty()) {
    s = clip(b.page.title.empty() ? b.url : b.page.title, 70) + "    line " + std::to_string(std::min(b.page.lines.size(), b.scroll + (li == ~0u ? 0 : li) + 1)) + "/" + std::to_string(b.page.lines.size());
  } else s = b.status;
  host.setLabel(b.statusHwnd, widen(s));
}

void selectFirstLink(Browser& b, PageDef& pg) {
  unsigned li = focusedLine(b, pg);
  b.curLink = -1;
  if (li == ~0u) return;
  size_t idx = b.scroll + li;
  if (idx < b.page.lines.size() && !b.page.lines[idx].links.empty()) b.curLink = b.page.lines[idx].links[0];
}

void applyPage(Host& host, Browser& b, PageDef& pg, web::Rendered page, const std::string& url, bool push) {
  b.page = std::move(page);
  b.url = url;
  if (push) {
    if (b.histPos >= 0 && size_t(b.histPos) + 1 < b.history.size()) b.history.resize(size_t(b.histPos) + 1);
    if (b.history.empty() || b.history.back() != url) { b.history.push_back(url); b.histPos = int(b.history.size()) - 1; }
  }
  b.scroll = 0; b.loading = false;
  host.setLabel(b.addressHwnd, widen(url));
  showLines(host, b);
  if (b.history.size() > 1 || b.url != "about:home") host.setFocus(kToolbarItems);   // first content line (the very first page leaves the focus on the address bar)
  b.lastFocus = ~0u;
  selectFirstLink(b, pg);
  updateStatus(host, b, pg);
}

void navigate(Host& host, Browser& b, PageDef& pg, std::string target, bool push = true) {
  if (target == "about:home") { applyPage(host, b, pg, web::renderHtml(kHomeHtml, "about:home", kContentWidthPx), "about:home", push); return; }
  web::Url u;
  if (!web::parseUrl(target, u)) { b.status = "Not a web address"; b.page = web::Rendered(); showLines(host, b); updateStatus(host, b, pg); return; }
  target = u.str();
  b.url = target; b.loading = true; ++b.loadId;
  host.setLabel(b.addressHwnd, widen(target));
  updateStatus(host, b, pg);
  b.pendingNavigate.clear();
#ifdef __linux__
  auto f = b.fetch;
  uint64_t id = b.loadId;
  { std::lock_guard<std::mutex> lk(f->m); f->done = false; f->id = id; }
  std::thread([f, target, id]() {
    web::Response r = web::get(target);
    std::lock_guard<std::mutex> lk(f->m);
    if (f->id == id) { f->response = std::move(r); f->done = true; }
  }).detach();
#else
  b.loading = false; b.status = "Networking is only available in the Linux build";
  updateStatus(host, b, pg);
#endif
  (void)push;
}

void goBack(Host& host, Browser& b, PageDef& pg) {
  if (b.histPos <= 0) return;
  --b.histPos;
  navigate(host, b, pg, b.history[size_t(b.histPos)], false);
}

#ifdef __linux__
void pollFetch(Host& host, Browser& b, PageDef& pg) {
  if (!b.loading) return;
  web::Response r;
  {
    std::lock_guard<std::mutex> lk(b.fetch->m);
    if (!b.fetch->done || b.fetch->id != b.loadId) return;
    r = std::move(b.fetch->response);
    b.fetch->done = false;
  }
  if (!r.error.empty()) {
    web::Rendered err;
    err.lines.push_back(web::Line{"Could not load " + clip(r.finalUrl, 90), {}});
    err.lines.push_back(web::Line{r.error, {}});
    b.loading = false;
    applyPage(host, b, pg, err, b.url, false);
    return;
  }
  std::string type = r.headers.count("content-type") ? r.headers["content-type"] : "";
  std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
  web::Rendered page;
  if (type.find("html") != std::string::npos || type.empty() || r.body.find("<html") != std::string::npos || r.body.find("<HTML") != std::string::npos)
    page = web::renderHtml(r.body, r.finalUrl, kContentWidthPx);
  else if (type.find("text/") == 0 || type.find("json") != std::string::npos || type.find("xml") != std::string::npos)
    page = web::renderPlain(r.body, kContentWidthPx);
  else { page.lines.push_back(web::Line{"This kind of content cannot be shown (" + clip(type, 60) + ", " + std::to_string(r.body.size()) + " bytes).", {}}); }
  if (r.status >= 400 && page.lines.empty()) page.lines.push_back(web::Line{"HTTP error " + std::to_string(r.status), {}});
  bool push = b.history.empty() || b.history[size_t(b.histPos)] != r.finalUrl;
  // a back navigation re-loads an entry that is already in the history: do not push it again
  if (b.histPos >= 0 && size_t(b.histPos) < b.history.size() && b.history[size_t(b.histPos)] == b.url) push = false;
  applyPage(host, b, pg, std::move(page), r.finalUrl, push);
}
#endif

}  // namespace

void requestBrowserPage(Host& host) {
  static PagePtr page;
  static std::shared_ptr<Browser> br;
  if (page) { host.request(page); return; }
  br = std::make_shared<Browser>();
  auto pg = std::make_shared<PageDef>();
  page = pg; br->self = pg;
  pg->name = "browser";
  constexpr int pageW = 1300, pageH = 716, x0 = 14;
  Win root; root.cls = 0; root.flags = 0x81; root.w = pageW; root.h = pageH; root.x = (1360 - pageW) / 2; root.y = 26;
  pg->add(root);
  Win panel; panel.w = pageW; panel.h = pageH; panel.normal = {kPanelTeal};
  pg->add(panel);

  auto button = [&](int x, int w, const char16_t* label) {
    Win b; b.cls = 3; b.x = uint16_t(x); b.y = 12; b.w = uint16_t(w); b.h = 46;
    b.normal = {kFillDark, comp(3, kTextCentre)}; b.focus = {kFillHighlight, comp(3, kTextCentreDark)}; b.disabled = {kFillDark, comp(3, kTextCentreGrey)};
    Item it; it.row = 0; it.cx = x + w / 2; it.hwnd = pg->add(b, label); pg->items.push_back(it);
  };
  button(x0, 110, u"Back");
  {
    Win a; a.cls = 3; a.x = 134; a.y = 12; a.w = 1020; a.h = 46;
    a.normal = {kFillBlack, comp(3, kTextLeft)}; a.focus = {kFillHighlight, comp(3, kTextLeftDark)}; a.disabled = a.normal;
    Item it; it.row = 0; it.cx = 134 + 510; br->addressHwnd = it.hwnd = pg->add(a, u" "); pg->items.push_back(it);
  }
  button(1164, 122, u"Close");
  br->firstLine = unsigned(pg->windows.size());
  for (unsigned i = 0; i < kVisible; ++i) {
    Win l; l.cls = 3; l.x = x0; l.y = uint16_t(70 + 36 * int(i)); l.w = pageW - 2 * x0; l.h = 36;
    l.normal = {comp(3, kTextLeft)}; l.focus = {kFillHighlight, comp(3, kTextLeftDark)}; l.disabled = l.normal;
    Item it; it.row = 1 + i; it.cx = l.x + l.w / 2; it.hwnd = pg->add(l, u" "); pg->items.push_back(it);
  }
  {
    Win s; s.cls = 3; s.x = x0; s.y = uint16_t(70 + 36 * int(kVisible) + 6); s.w = pageW - 2 * x0; s.h = 40;
    s.normal = {comp(3, kTextLeft)}; s.focus = s.normal; s.disabled = s.normal;
    br->statusHwnd = pg->add(s, u"Ready");
  }
  pg->focus = 1;   // the address bar

  Browser* b = br.get();
  pg->onPress = [b](Host& host, unsigned item) {
    PageDef& p = *b->self;
    if (item == 0) goBack(host, *b, p);
    else if (item == 1) {
      requestKeyboard(host, b->url == "about:home" ? std::string() : b->url, [b](bool accepted, const std::string& t) { if (accepted && !t.empty()) b->pendingNavigate = t; }, b->self);
    } else if (item == 2) host.closeAll();
    else if (b->curLink >= 0 && b->curLink < int(b->page.links.size())) navigate(host, *b, p, b->page.links[size_t(b->curLink)]);
  };
  pg->onKey = [b](Host& host, const std::string& f) {
    PageDef& p = *b->self;
    unsigned li = focusedLine(*b, p);
    if (f == "OSK_BACKSPACE") { goBack(host, *b, p); return true; }
    if (f == "PAGEDOWN" || f == "PAGEUP") {
      size_t maxScroll = b->page.lines.size() > kVisible ? b->page.lines.size() - kVisible : 0;
      size_t ns = f == "PAGEDOWN" ? std::min(maxScroll, b->scroll + kVisible - 1) : (b->scroll > kVisible - 1 ? b->scroll - (kVisible - 1) : 0);
      if (ns != b->scroll) { b->scroll = ns; showLines(host, *b); selectFirstLink(*b, p); updateStatus(host, *b, p); }
      return true;
    }
    if (li == ~0u) return false;
    if (f == "DOWN") {
      if (li + 1 < kVisible && b->scroll + li + 1 < b->page.lines.size()) return false;           // the engine moves the highlight down
      if (b->scroll + kVisible < b->page.lines.size()) { ++b->scroll; showLines(host, *b); selectFirstLink(*b, p); updateStatus(host, *b, p); }
      return true;
    }
    if (f == "UP") {
      if (li > 0) return false;
      if (b->scroll > 0) { --b->scroll; showLines(host, *b); selectFirstLink(*b, p); updateStatus(host, *b, p); return true; }
      return false;                                                                                 // up into the toolbar
    }
    if (f == "LEFT" || f == "RIGHT") {
      size_t idx = b->scroll + li;
      if (idx < b->page.lines.size()) {
        const auto& links = b->page.lines[idx].links;
        if (links.size() > 1) {
          size_t at = size_t(std::find(links.begin(), links.end(), b->curLink) - links.begin());
          at = f == "RIGHT" ? (at + 1) % links.size() : (at + links.size() - 1) % links.size();
          b->curLink = links[at];
          updateStatus(host, *b, p);
        }
      }
      return true;
    }
    return false;
  };
  pg->onService = [b](Host& host) {
    PageDef& p = *b->self;
    if (b->lastFocus == ~0u && b->page.lines.empty() && b->history.empty() && !b->loading) { navigate(host, *b, p, "about:home"); }
    if (!b->pendingNavigate.empty()) { std::string t = b->pendingNavigate; b->pendingNavigate.clear(); navigate(host, *b, p, t); }
#ifdef __linux__
    pollFetch(host, *b, p);
#endif
    if (p.focus != b->lastFocus) {
      b->lastFocus = p.focus;
      selectFirstLink(*b, p);
      updateStatus(host, *b, p);
    }
  };
  pg->onClosed = [b](Host&) { b->shown.clear(); b->lastFocus = ~0u; };
  host.request(pg);
}

}  // namespace pages
