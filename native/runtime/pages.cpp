#include "pages.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pages {
namespace {

constexpr uint32_t kSlot = 17;                       // ZUI page slot taken over while a custom page is open
constexpr uint32_t kDescriptors = 0x80686030u;       // 48 bytes per page id
constexpr uint32_t kStyleArray = 0x806A0178u;        // per page: pointer to 12-byte style-list records (one per window)
constexpr uint32_t kRectArray = 0x80695780u;         // per page: pointer to 12-byte rectangle records (one per window)
constexpr uint32_t kBase = 0x8F000000u;              // free guest RAM (the board has 128 MB; this is the unused upper half of the 256 MB backing)
constexpr uint32_t kTextBuffers = kBase + 0x20000u;  // 256 bytes of UTF-16 text per window (up to 512 windows)
constexpr uint32_t kZuiMode = 0x806D7E98u;           // 1 = no page shown
constexpr uint32_t kSurfacePool = 0x806D87F8u;       // pointer to the surface-pool manager; its slot table has the page-surface slot at +56
constexpr uint32_t kSurfaceFlag = 0x806D4F9Cu;       // bit 0: the page surface exists

constexpr uint32_t kFnSelectPage = 0x804C3684u;      // original page select (a0 = page id)
constexpr uint32_t kFnHidePage = 0x804C3574u;        // original hide of the active page
constexpr uint32_t kFnSetFocus = 0x8049FF00u;        // SetFocus(window handle)
constexpr uint32_t kFnRepaint = 0x8049F78Cu;         // paint window (window handle)
constexpr uint32_t kFnShow = 0x804A0460u;            // ShowWindow(window handle, 1 = show / 0 = hide)

inline uint8_t* P(CPU& c, uint32_t a) { return c.ram + (a & 0x1fffffffu); }
inline uint32_t R32(CPU& c, uint32_t a) { uint32_t v; std::memcpy(&v, P(c, a), 4); return v; }
inline void W32(CPU& c, uint32_t a, uint32_t v) { std::memcpy(P(c, a), &v, 4); }
inline void W16(CPU& c, uint32_t a, uint16_t v) { std::memcpy(P(c, a), &v, 2); }

}  // namespace

struct HostHooks {
  static Host* host;
  static bool selectBusy;
  static bool patchRect(CPU& c, void*);
  static bool textGetter(CPU& c, void*);
  static bool blockOtherPages(CPU& c, void*);
  static bool selectDone(CPU&, void*);
};
Host* HostHooks::host = nullptr;
bool HostHooks::selectBusy = false;

// The original page-open function of slot 17 (804DC64C) hard-codes the page rectangle in a stack structure that it passes to the surface setup 8049DFC4; while a
// custom page is open this hook rewrites that rectangle in place (x, y, w, h as 16-bit values) and then lets the ORIGINAL code run unchanged.
bool HostHooks::patchRect(CPU& c, void*) {
  Host* h = host;
  if (!h || !h->current_ || h->state_ == Host::kIdle || c.r[31] != 0x804DC720u || h->current_->windows.empty()) return false;
  const Win& root = h->current_->windows[0];
  W16(c, c.r[4] + 0, root.x); W16(c, c.r[4] + 2, root.y); W16(c, c.r[4] + 4, root.w); W16(c, c.r[4] + 6, root.h);
  return false;
}

// Text: the page's text getter (804DC584: window handle -> POINTER to NUL-terminated UTF-16LE text; the original getters return the firmware's string buffer)
// returns a per-window text buffer in host-owned guest RAM.
bool HostHooks::textGetter(CPU& c, void*) {
  Host* h = host;
  if (!h || !h->current_ || h->state_ == Host::kIdle || h->state_ == Host::kPending) return false;
  const uint32_t k = c.r[4] & 0xffffu;
  const std::u16string empty;
  const std::u16string& t = k < h->current_->labels.size() ? h->current_->labels[k] : empty;
  const uint32_t at = kTextBuffers + 256u * (k & 0x1ffu);
  uint8_t* out = P(c, at);
  size_t n = t.size() < 120 ? t.size() : 120;
  for (size_t i = 0; i < n; ++i) { out[2 * i] = uint8_t(t[i]); out[2 * i + 1] = uint8_t(t[i] >> 8); }
  out[2 * n] = 0; out[2 * n + 1] = 0;
  c.r[2] = at;
  return true;
}

// While a custom page is open no other page may be selected: the firmware re-selects page 2 (the NO SIGNAL dialog) every few million instructions, which would
// replace ours. Selections other than ours (page 17) are ignored (the call returns 0 at once).
bool HostHooks::blockOtherPages(CPU& c, void*) {
  Host* h = host;
  if (!h) return false;
  const bool ours = (c.r[4] & 0xffu) == kSlot;
  if ((h->state_ == Host::kOpening || h->state_ == Host::kOpen) && !ours) { c.r[2] = 0; return true; }
  selectBusy = true;
  return false;
}
// 804C37A0 is the common continuation after a page's open function: the select is finished.
bool HostHooks::selectDone(CPU&, void*) { selectBusy = false; return false; }

void Host::attach(RunHooks& hooks) {
  HostHooks::host = this;
  auto add = [&](uint32_t pc, bool (*fn)(CPU&, void*)) { if (hooks.entry_n < 16) hooks.entry[hooks.entry_n++] = EntryHook{pc, fn, nullptr}; };
  add(0x8049DFC4u, HostHooks::patchRect);
  add(0x804DC584u, HostHooks::textGetter);
  add(kFnSelectPage, HostHooks::blockOtherPages);
  add(0x804C37A0u, HostHooks::selectDone);
}

// Writes the page's tables into guest RAM and points slot 17's lookups at them (the originals are saved and put back by release()).
void Host::takeover(CPU& c) {
  const PageDef& pg = *current_;
  const unsigned n = unsigned(pg.windows.size());
  const uint32_t d = kDescriptors + 48 * kSlot;
  if (!saved_.valid) {
    saved_.valid = true;
    saved_.count = R32(c, d); saved_.table = R32(c, d + 4);
    saved_.styles = R32(c, kStyleArray + 4 * kSlot); saved_.rects = R32(c, kRectArray + 4 * kSlot);
  }
  const uint32_t winTable = kBase, rects = kBase + 0x4000, styles = kBase + 0x8000, lists = kBase + 0xC000;   // room for 512 windows each
  uint32_t listCursor = lists;
  auto emitList = [&](const std::vector<uint16_t>& l) -> uint32_t {
    if (l.empty()) return 0;
    uint32_t at = listCursor;
    for (uint16_t v : l) { W16(c, listCursor, v); listCursor += 2; }
    W16(c, listCursor, 0); listCursor += 2;     // type 0 ends the list
    listCursor = (listCursor + 3) & ~3u;
    return at;
  };
  for (unsigned k = 0; k < n; ++k) {
    const Win& w = pg.windows[k];
    uint32_t wr = winTable + 20 * k;
    std::memset(P(c, wr), 0, 20);
    W32(c, wr + 0, w.cls);
    W32(c, wr + 8, w.flags);
    uint32_t rr = rects + 12 * k;
    W16(c, rr + 0, 0); W16(c, rr + 2, uint16_t(k == 0 ? n - 1 : k));
    W16(c, rr + 4, k == 0 ? 0 : w.x); W16(c, rr + 6, k == 0 ? 0 : w.y); W16(c, rr + 8, w.w); W16(c, rr + 10, w.h);
    uint32_t sr = styles + 12 * k;
    W32(c, sr + 0, emitList(w.normal));
    W32(c, sr + 4, emitList(w.focus));
    W32(c, sr + 8, emitList(w.disabled));
  }
  W32(c, d + 0, n);
  W32(c, d + 4, winTable);
  W32(c, kStyleArray + 4 * kSlot, styles);
  W32(c, kRectArray + 4 * kSlot, rects);
}

void Host::release(CPU& c) {
  if (!saved_.valid) return;
  const uint32_t d = kDescriptors + 48 * kSlot;
  W32(c, d + 0, saved_.count); W32(c, d + 4, saved_.table);
  W32(c, kStyleArray + 4 * kSlot, saved_.styles); W32(c, kRectArray + 4 * kSlot, saved_.rects);
  saved_.valid = false;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void Host::request(PagePtr page) {
  next_ = std::move(page);
  if (state_ == kOpening || state_ == kOpen) beginClose();
  else if (state_ == kIdle) { state_ = kPending; hideSent_ = false; }
}
void Host::closeAll() {
  next_.reset();
  if (state_ == kOpening || state_ == kOpen) beginClose();
  else if (state_ == kPending) state_ = kIdle;
}
void Host::beginClose() {
  state_ = kClosing; closingAt_ = 0;
  acts_.clear();
  queue(kFnHidePage, 0);
}
void Host::setLabel(unsigned hwnd, const std::u16string& text) {
  if (!current_ || hwnd >= current_->labels.size()) return;
  if (std::getenv("MANTA_PAGES_DEBUG")) std::fprintf(stderr, "pages: setLabel hwnd %u len %zu (state %d)%c", hwnd, text.size(), int(state_), 10);
  current_->labels[hwnd] = text;
  queue(kFnRepaint, hwnd);
}
void Host::setFocus(unsigned item) {
  if (!current_ || item >= current_->items.size()) return;
  current_->focus = item;
  queue(kFnSetFocus, current_->items[item].hwnd);
}
void Host::showWindow(unsigned hwnd, bool show) { queue(kFnShow, hwnd, show ? 1 : 0); }

static void moveFocus(Host& host, PageDef& pg, int dx, int dy) {
  if (pg.items.empty()) return;
  const Item& cur = pg.items[pg.focus];
  int best = -1, bestScore = 1 << 30;
  for (unsigned i = 0; i < pg.items.size(); ++i) {
    const Item& it = pg.items[i];
    int score;
    if (dx) {
      if (it.row != cur.row || (dx > 0 ? it.cx <= cur.cx : it.cx >= cur.cx)) continue;
      score = (it.cx > cur.cx ? it.cx - cur.cx : cur.cx - it.cx);
    } else {
      if (dy > 0 ? it.row <= cur.row : it.row >= cur.row) continue;
      int rowDist = it.row > cur.row ? int(it.row - cur.row) : int(cur.row - it.row);
      score = rowDist * 10000 + (it.cx > cur.cx ? it.cx - cur.cx : cur.cx - it.cx);
    }
    if (score < bestScore) { bestScore = score; best = int(i); }
  }
  if (best >= 0) host.setFocus(unsigned(best));
}

bool Host::onFunction(const std::string& f) {
  if (state_ == kIdle) {
    if (f == "OSK") {
      requestKeyboard(*this, "", [](bool accepted, const std::string& text) { if (accepted) std::fprintf(stderr, "OSK_RESULT %s%c", text.c_str(), 10); });
      return true;
    }
    if (f == "NET") { requestNetworkPage(*this); return true; }
    return false;
  }
  if (state_ != kOpen || !current_) return true;       // pending / opening / closing: swallow keys
  PageDef& pg = *current_;
  if (pg.onKey && pg.onKey(*this, f)) return true;
  if (f == "UP") moveFocus(*this, pg, 0, -1);
  else if (f == "DOWN") moveFocus(*this, pg, 0, 1);
  else if (f == "LEFT") moveFocus(*this, pg, -1, 0);
  else if (f == "RIGHT") moveFocus(*this, pg, 1, 0);
  else if (f == "OK") { if (pg.onPress && pg.focus < pg.items.size()) pg.onPress(*this, pg.focus); }
  else if (f == "EXIT") closeAll();
  return true;   // everything else is swallowed while a page is up
}

bool Host::runAct(CPU& c) {
  if (acts_.empty()) return false;
  Act a = acts_.front(); acts_.pop_front();
  c.inject.target = a.target; c.inject.a0 = a.a0; c.inject.a1 = a.a1; c.inject.pending = true;
  return true;
}

void Host::service(CPU& c, uint64_t d) {
  now_ = d;
  static uint32_t peekAddr = 0, peekLen = 4; static std::string peekLast; static bool peekInit = false;
  if (!peekInit) { peekInit = true; if (const char* e = std::getenv("MANTA_PEEK")) { char* end = nullptr; peekAddr = uint32_t(std::strtoul(e, &end, 16)); if (*end == ':') peekLen = uint32_t(std::strtoul(end + 1, nullptr, 16)); } }
  if (peekAddr) {
    std::string now(reinterpret_cast<const char*>(P(c, peekAddr)), peekLen);
    if (now != peekLast) {
      std::fprintf(stderr, "PEEK %08x at dispatch %llu:", peekAddr, (unsigned long long)d);
      for (uint32_t i = 0; i < peekLen; ++i) std::fprintf(stderr, "%s%02x", (i & 7) == 0 ? " | " : " ", (unsigned char)now[i]);
      std::fprintf(stderr, "%c", 10);
      peekLast = now;
    }
  }
  if (testOskAt && !testDone_ && d >= testOskAt) { testDone_ = true; onFunction("OSK"); }
  if (activeFlag) activeFlag->store(state_ != kIdle);
  if (c.injecting || c.inject.pending) return;

  const uint32_t mode = R32(c, kZuiMode);
  switch (state_) {
    case kIdle: return;
    case kClosing: {
      if (runAct(c)) return;
      if (closingAt_ == 0) closingAt_ = d;
      if (mode == 1 || d > closingAt_ + 30000000) {   // the page is hidden: give slot 17 back
        release(c);
        PagePtr closed = std::move(current_);
        acts_.clear();
        state_ = next_ ? kPending : kIdle;
        hideSent_ = false;
        std::fprintf(stderr, "pages: closed '%s' at dispatch %llu%c", closed ? closed->name.c_str() : "?", (unsigned long long)d, 10);
        if (closed && closed->onClosed) closed->onClosed(*this);
        if (activeFlag) activeFlag->store(state_ != kIdle);
      }
      return;
    }
    case kOpening:
    case kOpen: {
      if (needInit_ && d > openedAt_ + 200000) {
        // The original page-enter code of slot 17 (804D975C...) hides all windows and shows only the ones of the channel banner: show every window of the page.
        needInit_ = false; state_ = kOpen;
        for (unsigned k = 1; k < current_->windows.size(); ++k) queue(kFnShow, k, 1);
        setFocus(current_->focus);
        std::fprintf(stderr, "pages: '%s' open (%u windows) at dispatch %llu%c", current_->name.c_str(), unsigned(current_->windows.size()), (unsigned long long)d, 10);
      }
      if (runAct(c)) return;
      if (state_ == kOpen && current_->onService) current_->onService(*this);
      return;
    }
    case kPending: {
      if (!next_) { state_ = kIdle; return; }
      if (HostHooks::selectBusy) return;   // never interleave with a page creation that is still running
      // The firmware only opens a page while the surface pool has a free slot: its surface setup (8049DFC4) hangs on purpose otherwise. The pool manager (struct at
      // *806D87F8) keeps its slot table at M[0]; slot 5 (offset 56, state byte +4 == 0xFF when free) is the one a page surface takes. The page that is open releases it
      // when it is hidden, so: ask the open page to hide (only while its surface exists), wait until the ZUI is idle (mode 1) and the slot is free, then select ours.
      const uint32_t pool = R32(c, R32(c, kSurfacePool));
      const bool slotFree = *P(c, pool + 56 + 4) == 0xff;
      if (mode == 1 && slotFree) {
        current_ = std::move(next_);
        takeover(c);
        state_ = kOpening; needInit_ = true; openedAt_ = d; acts_.clear();
        c.inject.target = kFnSelectPage; c.inject.a0 = kSlot; c.inject.pending = true;
      } else if (mode != 1 && (R32(c, kSurfaceFlag) & 1) && (!hideSent_ || d > hideAt_ + 5000000)) {
        hideSent_ = true; hideAt_ = d;
        c.inject.target = kFnHidePage; c.inject.a0 = 0; c.inject.pending = true;
      }
      return;
    }
  }
}

}  // namespace pages
