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
    "<h2>Search</h2><form action=\"https://en.wikipedia.org/w/index.php\" method=\"get\"><input type=\"text\" name=\"search\" placeholder=\"Search Wikipedia\"> <input type=\"submit\" value=\"Search\"></form>"
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
  int curInput = -1;                   // form field selected on the highlighted line (exclusive with curLink)
  std::string html;                    // raw body of the current page (re-rendered when a form field changes)
  bool isHtml = false;
  std::vector<web::InputState> istate; // user's changes to the form fields of the current page
  int refreshCount = 0;
  int pendingFormEdit = -1;            // field index whose text was just entered on the keyboard
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
  else if (li != ~0u && b.curInput >= 0 && b.curInput < int(b.page.inputs.size())) {
    const web::Input& in = b.page.inputs[size_t(b.curInput)];
    if (in.tag == "select") s = "Choice " + (in.name.empty() ? std::string("list") : in.name) + ": OK = next option";
    else if (in.type == "checkbox" || in.type == "radio") s = std::string(in.type == "radio" ? "Radio " : "Checkbox ") + in.name + ": OK = toggle";
    else if (in.type == "submit" || in.type == "button" || in.type == "image" || in.tag == "button") s = "Button: OK = send form";
    else if (in.type == "reset") s = "Button: OK = reset form";
    else s = "Text field " + in.name + ": OK = type";
  }
  else if (li != ~0u && b.curLink >= 0 && b.curLink < int(b.page.links.size())) s = "[" + std::to_string(b.curLink + 1) + "] " + clip(b.page.links[size_t(b.curLink)], 110);
  else if (!b.page.lines.empty()) {
    s = clip(b.page.title.empty() ? b.url : b.page.title, 70) + "    line " + std::to_string(std::min(b.page.lines.size(), b.scroll + (li == ~0u ? 0 : li) + 1)) + "/" + std::to_string(b.page.lines.size());
  } else s = b.status;
  host.setLabel(b.statusHwnd, widen(s));
}

void selectFirstLink(Browser& b, PageDef& pg) {
  unsigned li = focusedLine(b, pg);
  b.curLink = -1; b.curInput = -1;
  if (li == ~0u) return;
  size_t idx = b.scroll + li;
  if (idx >= b.page.lines.size()) return;
  if (!b.page.lines[idx].links.empty()) b.curLink = b.page.lines[idx].links[0];
  else if (!b.page.lines[idx].inputs.empty()) b.curInput = b.page.lines[idx].inputs[0];
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

void navigateReq(Host& host, Browser& b, PageDef& pg, web::Request req, bool push) {
  std::string target = req.url;
  if (target == "about:home") {
    b.html = kHomeHtml; b.isHtml = true;
    web::Rendered r = web::renderHtml(b.html, "about:home", kContentWidthPx);
    b.istate.assign(r.inputs.size(), web::InputState());
    applyPage(host, b, pg, std::move(r), "about:home", push); return;
  }
  web::Url u;
  if (!web::parseUrl(target, u)) { b.status = "Not a web address"; b.page = web::Rendered(); showLines(host, b); updateStatus(host, b, pg); return; }
  req.url = target = u.str();
  b.url = target; b.loading = true; ++b.loadId;
  host.setLabel(b.addressHwnd, widen(target));
  updateStatus(host, b, pg);
  b.pendingNavigate.clear();
#ifdef __linux__
  auto f = b.fetch;
  uint64_t id = b.loadId;
  { std::lock_guard<std::mutex> lk(f->m); f->done = false; f->id = id; }
  std::thread([f, req, id]() {
    web::Response r = web::fetch(req);
    std::lock_guard<std::mutex> lk(f->m);
    if (f->id == id) { f->response = std::move(r); f->done = true; }
  }).detach();
#else
  b.loading = false; b.status = "Networking is only available in the Linux build";
  updateStatus(host, b, pg);
#endif
  (void)push;
}

void navigate(Host& host, Browser& b, PageDef& pg, std::string target, bool push = true) {
  web::Request q; q.url = std::move(target);
  navigateReq(host, b, pg, std::move(q), push);
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
  std::string body = r.body;
  if (r.charset.find("8859-1") != std::string::npos || r.charset == "latin1" || r.charset.find("1252") != std::string::npos) body = web::latin1ToUtf8(body);
  b.isHtml = false; b.html.clear(); b.istate.clear();
  if (type.find("html") != std::string::npos || type.empty() || body.find("<html") != std::string::npos || body.find("<HTML") != std::string::npos) {
    page = web::renderHtml(body, r.finalUrl, kContentWidthPx);
    b.html = std::move(body); b.isHtml = true;
    b.istate.resize(page.inputs.size());
    for (size_t i = 0; i < page.inputs.size(); ++i) { b.istate[i].value = page.inputs[i].value; b.istate[i].checked = page.inputs[i].checked; b.istate[i].selected = page.inputs[i].selected; }
  } else if (type.find("text/") == 0 || type.find("json") != std::string::npos || type.find("xml") != std::string::npos)
    page = web::renderPlain(r.body, kContentWidthPx);
  else { page.lines.push_back(web::Line{"This kind of content cannot be shown (" + clip(type, 60) + ", " + std::to_string(r.body.size()) + " bytes).", {}}); }
  if (r.status >= 400 && page.lines.empty()) page.lines.push_back(web::Line{"HTTP error " + std::to_string(r.status), {}});
  bool push = b.history.empty() || b.history[size_t(b.histPos)] != r.finalUrl;
  // a back navigation re-loads an entry that is already in the history: do not push it again
  if (b.histPos >= 0 && size_t(b.histPos) < b.history.size() && b.history[size_t(b.histPos)] == b.url) push = false;
  std::string refresh = page.refreshUrl;
  applyPage(host, b, pg, std::move(page), r.finalUrl, push);
  if (!refresh.empty() && refresh != r.finalUrl && b.refreshCount < 3) { ++b.refreshCount; b.pendingNavigate = refresh; }
}
#endif


bool isTextField(const web::Input& in) {
  if (in.tag == "textarea") return true;
  if (in.tag != "input") return false;
  static const char* other[] = {"hidden", "checkbox", "radio", "submit", "button", "reset", "image", "file", "range", "color"};
  for (const char* o : other) if (in.type == o) return false;
  return true;
}

void rerender(Host& host, Browser& b, PageDef& pg) {
  if (!b.isHtml) return;
  std::string refreshKeep = b.page.refreshUrl;
  b.page = web::renderHtml(b.html, b.url, kContentWidthPx, &b.istate);
  b.page.refreshUrl = refreshKeep;
  size_t maxScroll = b.page.lines.size() > kVisible ? b.page.lines.size() - kVisible : 0;
  if (b.scroll > maxScroll) b.scroll = maxScroll;
  showLines(host, b);
  int keep = b.curInput;
  selectFirstLink(b, pg);
  if (keep >= 0) { b.curLink = -1; b.curInput = keep; }
  updateStatus(host, b, pg);
}

web::Request buildSubmit(const Browser& b, int formIdx, int clicked) {
  web::Request q;
  const web::Form& f = b.page.forms[size_t(formIdx)];
  std::string data;
  auto add = [&](const std::string& k, const std::string& v) { if (!data.empty()) data += "&"; data += web::urlEncode(k) + "=" + web::urlEncode(v); };
  const web::InputState none;
  for (size_t i = 0; i < b.page.inputs.size(); ++i) {
    const web::Input& in = b.page.inputs[i];
    if (in.form != formIdx || in.name.empty()) continue;
    const web::InputState& st = i < b.istate.size() ? b.istate[i] : none;
    if (in.tag == "select") { if (!in.options.empty()) add(in.name, in.options[size_t(std::min<int>(st.selected, int(in.options.size()) - 1))].first); continue; }
    if (in.type == "submit" || in.type == "button" || in.type == "image" || in.type == "reset" || in.tag == "button") {
      if (int(i) == clicked) { if (in.type == "image") { add(in.name + ".x", "1"); add(in.name + ".y", "1"); } else add(in.name, in.value); }
      continue;
    }
    if (in.type == "checkbox" || in.type == "radio") { if (st.checked) add(in.name, in.value.empty() ? std::string("on") : in.value); continue; }
    add(in.name, st.value);
  }
  q.method = f.method;
  q.url = f.action;
  if (f.method == "POST") q.body = data;
  else {
    size_t cut = q.url.find_first_of("#?");
    if (cut != std::string::npos) q.url.resize(cut);
    q.url += (data.empty() ? "" : "?") + data;
  }
  return q;
}

void submitForm(Host& host, Browser& b, PageDef& pg, int formIdx, int clicked) {
  if (formIdx < 0 || formIdx >= int(b.page.forms.size())) { b.status = "This button is not inside a form"; updateStatus(host, b, pg); return; }
  b.refreshCount = 0;
  navigateReq(host, b, pg, buildSubmit(b, formIdx, clicked), true);
}

int textFieldsInForm(const Browser& b, int formIdx) {
  int n = 0;
  for (const auto& in : b.page.inputs) if (in.form == formIdx && isTextField(in)) ++n;
  return n;
}

void pressInput(Host& host, Browser& b, PageDef& pg) {
  if (b.curInput < 0 || b.curInput >= int(b.page.inputs.size()) || size_t(b.curInput) >= b.istate.size()) return;
  const int idx = b.curInput;
  const web::Input in = b.page.inputs[size_t(idx)];
  web::InputState& st = b.istate[size_t(idx)];
  if (in.tag == "select") {
    if (!in.options.empty()) st.selected = (st.selected + 1) % int(in.options.size());
    rerender(host, b, pg);
  } else if (in.type == "checkbox") {
    st.checked = !st.checked; rerender(host, b, pg);
  } else if (in.type == "radio") {
    for (size_t i = 0; i < b.page.inputs.size() && i < b.istate.size(); ++i)
      if (b.page.inputs[i].form == in.form && b.page.inputs[i].type == "radio" && b.page.inputs[i].name == in.name) b.istate[i].checked = false;
    st.checked = true; rerender(host, b, pg);
  } else if (in.type == "reset") {
    for (size_t i = 0; i < b.page.inputs.size() && i < b.istate.size(); ++i)
      if (b.page.inputs[i].form == in.form) { b.istate[i].value = b.page.inputs[i].value; b.istate[i].checked = b.page.inputs[i].checked; b.istate[i].selected = b.page.inputs[i].selected; }
    rerender(host, b, pg);
  } else if (in.type == "submit" || in.type == "button" || in.type == "image" || in.tag == "button") {
    if (in.type == "button" && in.tag == "input") { b.status = "This button needs JavaScript"; updateStatus(host, b, pg); return; }
    submitForm(host, b, pg, in.form, idx);
  } else if (isTextField(in)) {
    Browser* bp = &b;
    requestKeyboard(host, st.value, [bp, idx](bool accepted, const std::string& t) {
      if (!accepted || size_t(idx) >= bp->istate.size()) return;
      bp->istate[size_t(idx)].value = t;
      bp->pendingFormEdit = idx;
    }, b.self);
  }
}

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
    else if (b->curLink >= 0 && b->curLink < int(b->page.links.size()) && b->page.links[size_t(b->curLink)][0] == '#') {
      const std::string frag = b->page.links[size_t(b->curLink)].substr(1);
      for (const auto& a : b->page.anchors) if (a.first == frag) {
        size_t maxScroll = b->page.lines.size() > kVisible ? b->page.lines.size() - kVisible : 0;
        b->scroll = std::min<size_t>(size_t(a.second), maxScroll);
        showLines(host, *b); host.setFocus(kToolbarItems); b->lastFocus = ~0u; selectFirstLink(*b, p); updateStatus(host, *b, p);
        break;
      }
    }
    else if (b->curLink >= 0 && b->curLink < int(b->page.links.size())) { b->refreshCount = 0; navigate(host, *b, p, b->page.links[size_t(b->curLink)]); }
    else if (b->curInput >= 0) pressInput(host, *b, p);
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
        const auto& ln = b->page.lines[idx];
        std::vector<std::pair<bool, int>> t;            // (is input, index): the links of the line, then its form fields
        for (int l : ln.links) t.emplace_back(false, l);
        for (int i : ln.inputs) t.emplace_back(true, i);
        if (t.size() > 1) {
          size_t at = 0;
          for (size_t k = 0; k < t.size(); ++k) if (t[k].first ? t[k].second == b->curInput : (b->curInput < 0 && t[k].second == b->curLink)) at = k;
          at = f == "RIGHT" ? (at + 1) % t.size() : (at + t.size() - 1) % t.size();
          b->curLink = t[at].first ? -1 : t[at].second;
          b->curInput = t[at].first ? t[at].second : -1;
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
    if (b->pendingFormEdit >= 0) {
      int idx = b->pendingFormEdit; b->pendingFormEdit = -1;
      int formIdx = size_t(idx) < b->page.inputs.size() ? b->page.inputs[size_t(idx)].form : -1;
      b->curInput = idx; b->curLink = -1;
      rerender(host, *b, p);
      if (formIdx >= 0 && textFieldsInForm(*b, formIdx) == 1) submitForm(host, *b, p, formIdx, -1);   // a one-field form (search box) is sent like Enter does
    }
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
