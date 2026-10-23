#include "osk.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace osk {
namespace {

constexpr uint32_t kSlot = 17;                       // ZUI page slot taken over while the keyboard is open
constexpr uint32_t kDescriptors = 0x80686030u;       // 48 bytes per page id
constexpr uint32_t kStyleArray = 0x806A0178u;        // per page: pointer to 12-byte style-list records (one per window)
constexpr uint32_t kRectArray = 0x80695780u;         // per page: pointer to 12-byte rectangle records (one per window)
constexpr uint32_t kBase = 0x8F000000u;              // free guest RAM (the board has 128 MB; this is the unused upper half of the 256 MB backing)
constexpr uint32_t kTextBuffers = kBase + 0x9000u;   // 128 bytes of UTF-16 text per window
constexpr uint32_t kZuiMode = 0x806D7E98u;           // 1 = no page shown
constexpr uint32_t kSurfacePool = 0x806D87F8u;       // pointer to the surface-pool manager; its slot table has the page-surface slot at +56

// original functions called through the injection mechanism
constexpr uint32_t kFnSelectPage = 0x804C3684u;      // original page select (a0 = page id)
constexpr uint32_t kFnHidePage = 0x804C3574u;        // original hide of the active page
constexpr uint32_t kFnSetFocus = 0x8049FF00u;        // SetFocus(window handle)
constexpr uint32_t kFnRepaint = 0x8049F78Cu;         // paint window (window handle)
constexpr uint32_t kFnShow = 0x804A0460u;            // ShowWindow(window handle, 1 = show / 0 = hide)

// draw-style components: type in the low 5 bits, table index above (firmware 8049E7BC)
constexpr uint16_t comp(unsigned type, unsigned index) { return uint16_t(type | (index << 5)); }
constexpr uint16_t kFillDark = comp(2, 1), kFillBlack = comp(2, 0), kFillHighlight = comp(2, 4), kPanelTeal = comp(5, 2);   // existing type 2 rows 0/1/4 and type 5 row 2
constexpr unsigned kTextCentre = 210, kTextCentreDark = 211, kTextCentreGrey = 212, kTextLeft = 4;                          // existing type-3 rows (font, colour, alignment)

inline uint8_t* P(CPU& c, uint32_t a) { return c.ram + (a & 0x1fffffffu); }
inline uint32_t R32(CPU& c, uint32_t a) { uint32_t v; std::memcpy(&v, P(c, a), 4); return v; }
inline void W32(CPU& c, uint32_t a, uint32_t v) { std::memcpy(P(c, a), &v, 4); }
inline void W16(CPU& c, uint32_t a, uint16_t v) { std::memcpy(P(c, a), &v, 2); }

struct Win {
  uint32_t cls = 1, flags = 0x83;
  uint16_t x = 0, y = 0, w = 0, h = 0;
  std::vector<uint16_t> normal, focus, disabled;
};

enum Action { kChar, kShift, kSymbols, kBackspace, kSpace, kCancel, kOk };
enum Layer { kLower, kUpper, kSym };

struct Key {
  unsigned hwnd = 0;
  unsigned row = 0;
  int cx = 0;                         // horizontal centre in page coordinates (for up/down navigation)
  Action action = kChar;
  std::u16string label[3];            // label on the lower / upper / symbol layer
};

struct Saved {
  bool valid = false;
  uint32_t count = 0, table = 0, styles = 0, rects = 0;
};

struct Act { uint32_t target, a0, a1; };

Osk* g_osk = nullptr;
std::vector<Win> g_windows;
std::vector<std::u16string> g_labels;   // text of window k
std::vector<Key> g_keys;
unsigned g_fieldHwnd = 0, g_hintHwnd = 0;
unsigned g_focusKey = 0;                // index into g_keys
Layer g_layer = kLower;
Saved g_saved;
std::deque<Act> g_acts;
uint64_t g_openedAt = 0, g_closingAt = 0, g_hideAt = 0;
bool g_needFocus = false;
Win g_pageRect;

std::u16string widen(const char* s) { std::u16string o; while (*s) o.push_back(char16_t(uint8_t(*s++))); return o; }

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
// Page layout. A key unit is 66 px (60 px key + 6 px gap).
void buildPage() {
  g_windows.clear(); g_labels.clear(); g_keys.clear();
  unsigned maxKeys = 0;
  if (const char* e = std::getenv("MANTA_OSK_KEYS")) maxKeys = unsigned(std::strtoul(e, nullptr, 10));   // diagnostic: build only the first N keys
  constexpr int u = 66, x0 = 20, rowPitch = 50;
  int keyH = 44;
  if (const char* e = std::getenv("MANTA_OSK_KEYH")) keyH = std::atoi(e);   // diagnostic
  int firstRow = 84;
  if (const char* e = std::getenv("MANTA_OSK_FIRSTROW")) firstRow = std::atoi(e);   // diagnostic
  struct Spec { const char* lo; const char* up; const char* sym; float w; Action act; };
  const std::vector<std::vector<Spec>> rows = {
    {{"1","!","1",1,kChar},{"2","@","2",1,kChar},{"3","#","3",1,kChar},{"4","$","4",1,kChar},{"5","%","5",1,kChar},{"6","^","6",1,kChar},{"7","&","7",1,kChar},
     {"8","*","8",1,kChar},{"9","(","9",1,kChar},{"0",")","0",1,kChar},{"Del","Del","Del",1.5f,kBackspace}},
    {{"q","Q","~",1,kChar},{"w","W","`",1,kChar},{"e","E","|",1,kChar},{"r","R","\\",1,kChar},{"t","T","[",1,kChar},{"y","Y","]",1,kChar},{"u","U","{",1,kChar},
     {"i","I","}",1,kChar},{"o","O","<",1,kChar},{"p","P",">",1,kChar},{"@","@","@",1.5f,kChar}},
    {{"a","A",";",1,kChar},{"s","S",":",1,kChar},{"d","D","=",1,kChar},{"f","F","+",1,kChar},{"g","G","-",1,kChar},{"h","H","_",1,kChar},{"j","J","/",1,kChar},
     {"k","K","?",1,kChar},{"l","L","!",1,kChar},{"'","\"","\"",1,kChar},{"_","_","_",1.5f,kChar}},
    {{"Shift","Shift","Shift",1.5f,kShift},{"z","Z","#",1,kChar},{"x","X","$",1,kChar},{"c","C","%",1,kChar},{"v","V","&",1,kChar},{"b","B","*",1,kChar},{"n","N","(",1,kChar},
     {"m","M",")",1,kChar},{",",",",",",1,kChar},{".",".",".",1,kChar},{"/","/","/",1,kChar}},
    {{"123","abc","abc",1.5f,kSymbols},{"Space","Space","Space",5.5f,kSpace},{"Cancel","Cancel","Cancel",2.25f,kCancel},{"OK","OK","OK",2.25f,kOk}}};
  const int pageW = int(11.5f * u) + 2 * x0 - 6, pageH = firstRow + int(rows.size()) * rowPitch + 52;
  g_pageRect = Win{};
  g_pageRect.w = uint16_t(pageW); g_pageRect.h = uint16_t(pageH);
  g_pageRect.x = uint16_t((1360 - pageW) / 2); g_pageRect.y = uint16_t(768 - pageH - 24);

  auto add = [&](Win w, std::u16string label) { g_windows.push_back(w); g_labels.push_back(std::move(label)); return unsigned(g_windows.size() - 1); };
  Win root; root.cls = 0; root.flags = 0x81; root.x = g_pageRect.x; root.y = g_pageRect.y; root.w = g_pageRect.w; root.h = g_pageRect.h;
  add(root, {});
  Win panel; panel.w = g_pageRect.w; panel.h = g_pageRect.h; panel.normal = {kPanelTeal};
  add(panel, {});
  Win field; field.cls = 3; field.x = x0; field.y = 16; field.w = uint16_t(pageW - 2 * x0); field.h = 52;
  field.normal = {kFillBlack, comp(3, kTextLeft)}; field.focus = field.normal; field.disabled = field.normal;
  if (std::getenv("MANTA_OSK_NOFIELD")) g_fieldHwnd = 0; else g_fieldHwnd = add(field, u"_");   // diagnostic
  for (unsigned r = 0; r < rows.size(); ++r) {
    float cursor = 0;
    for (const Spec& s : rows[r]) {
      if (maxKeys && g_keys.size() >= maxKeys) break;
      Win k; k.cls = 3; k.x = uint16_t(x0 + int(cursor * u)); k.y = uint16_t(firstRow + int(r) * rowPitch); k.w = uint16_t(int(s.w * u) - 6); k.h = keyH;
      k.normal = {kFillDark, comp(3, kTextCentre)}; k.focus = {kFillHighlight, comp(3, kTextCentreDark)}; k.disabled = {kFillDark, comp(3, kTextCentreGrey)};
      Key key; key.row = r; key.action = s.act; key.cx = k.x + k.w / 2;
      key.label[0] = widen(s.lo); key.label[1] = widen(s.up); key.label[2] = widen(s.sym);
      key.hwnd = add(k, key.label[0]);
      g_keys.push_back(key);
      cursor += s.w;
    }
  }
  Win hint; hint.cls = 3; hint.x = x0; hint.y = uint16_t(firstRow + int(rows.size()) * rowPitch + 4); hint.w = uint16_t(pageW - 2 * x0); hint.h = 34;
  hint.normal = {comp(3, kTextCentre)};
  g_hintHwnd = add(hint, u"Arrows: move    OK: type    MENU: shift    EXIT: close");
  g_layer = kLower;
  g_focusKey = 0;
  for (unsigned i = 0; i < g_keys.size(); ++i) if (g_keys[i].label[0] == u"q") g_focusKey = i;
}

// Writes the page's tables into guest RAM and points slot 17's lookups at them (the originals are saved and put back by release()).
void takeover(CPU& c) {
  const unsigned n = unsigned(g_windows.size());
  const uint32_t d = kDescriptors + 48 * kSlot;
  if (!g_saved.valid) {
    g_saved.valid = true;
    g_saved.count = R32(c, d); g_saved.table = R32(c, d + 4);
    g_saved.styles = R32(c, kStyleArray + 4 * kSlot); g_saved.rects = R32(c, kRectArray + 4 * kSlot);
  }
  uint32_t winTable = kBase, rects = kBase + 0x2000, styles = kBase + 0x4000, lists = kBase + 0x6000;
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
    const Win& w = g_windows[k];
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

void release(CPU& c) {
  if (!g_saved.valid) return;
  const uint32_t d = kDescriptors + 48 * kSlot;
  W32(c, d + 0, g_saved.count); W32(c, d + 4, g_saved.table);
  W32(c, kStyleArray + 4 * kSlot, g_saved.styles); W32(c, kRectArray + 4 * kSlot, g_saved.rects);
  g_saved.valid = false;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
// Hooks. The original page-open function of slot 17 (804DC64C) hard-codes the page rectangle in a stack structure that it passes to the surface setup 8049DFC4;
// while the keyboard is open this hook rewrites that rectangle in place (x, y, w, h as 16-bit values) and then lets the ORIGINAL code run unchanged.
bool patchRect(CPU& c, void*) {
  if (!g_osk || !g_osk->active || c.r[31] != 0x804DC720u || g_windows.empty()) return false;
  W16(c, c.r[4] + 0, g_pageRect.x); W16(c, c.r[4] + 2, g_pageRect.y); W16(c, c.r[4] + 4, g_pageRect.w); W16(c, c.r[4] + 6, g_pageRect.h);
  return false;
}

// While the keyboard is open no other page may be selected: the firmware re-selects page 2 (the NO SIGNAL dialog) every few million instructions, which would replace
// the keyboard. Selections other than the keyboard's own (page 17) are ignored (the call returns 0 at once).
bool g_selectBusy = false;                  // a page select (804C3684 .. 804C37A0) is running: the firmware is in the middle of creating a page
bool blockOtherPages(CPU& c, void*) {
  if (!g_osk) return false;
  if (!g_osk->active || g_osk->closing || (c.r[4] & 0xffu) == kSlot) { g_selectBusy = true; return false; }
  c.r[2] = 0;
  return true;
}
bool selectDone(CPU&, void*) { g_selectBusy = false; return false; }

// Text. The page's draw-style list names a text-style row (type 3: font, colour, alignment) and the page's text getter (804DC584: window handle -> POINTER to the
// NUL-terminated UTF-16LE text to draw; the original getters return the firmware's string buffer) supplies the text. This getter returns a per-window text buffer
// in host-owned guest RAM.
bool textGetter(CPU& c, void*) {
  if (!g_osk || !g_osk->active) return false;
  const uint32_t k = c.r[4] & 0xffffu;
  const std::u16string empty;
  const std::u16string& t = k < g_labels.size() ? g_labels[k] : empty;
  const uint32_t at = kTextBuffers + 128u * k;
  uint8_t* out = P(c, at);
  size_t n = t.size() < 60 ? t.size() : 60;
  for (size_t i = 0; i < n; ++i) { out[2 * i] = uint8_t(t[i]); out[2 * i + 1] = uint8_t(t[i] >> 8); }
  out[2 * n] = 0; out[2 * n + 1] = 0;
  c.r[2] = at;
  return true;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
// Keyboard behaviour (host side; every visible effect is an original ZUI call queued in g_acts)
void queue(uint32_t target, uint32_t a0, uint32_t a1 = 0) { g_acts.push_back(Act{target, a0, a1}); }

void refreshText() {
  std::u16string t;
  for (char ch : g_osk->text) t.push_back(char16_t(uint8_t(ch)));
  t.push_back(u'_');
  g_labels[g_fieldHwnd] = t;
  queue(kFnRepaint, g_fieldHwnd);
}
void refreshKeys() {
  for (const Key& k : g_keys) {
    g_labels[k.hwnd] = k.label[g_layer];
    queue(kFnRepaint, k.hwnd);
  }
}
void setFocusKey(unsigned idx) {
  if (idx >= g_keys.size()) return;
  g_focusKey = idx;
  queue(kFnSetFocus, g_keys[idx].hwnd);
}
void moveFocus(int dx, int dy) {
  const Key& cur = g_keys[g_focusKey];
  if (dx) {
    int next = int(g_focusKey) + dx;
    if (next >= 0 && next < int(g_keys.size()) && g_keys[next].row == cur.row) setFocusKey(unsigned(next));
    return;
  }
  const int row = int(cur.row) + dy;
  int best = -1, bestDist = 1 << 30;
  for (unsigned i = 0; i < g_keys.size(); ++i)
    if (int(g_keys[i].row) == row) {
      int d = g_keys[i].cx > cur.cx ? g_keys[i].cx - cur.cx : cur.cx - g_keys[i].cx;
      if (d < bestDist) { bestDist = d; best = int(i); }
    }
  if (best >= 0) setFocusKey(unsigned(best));
}
void typeChar(char ch) {
  if (g_osk->text.size() < 56) g_osk->text.push_back(ch);
  refreshText();
}
void beginClose(bool accept) {
  if (g_osk->closing) return;
  g_osk->closing = true;
  g_osk->accepted = accept;
  if (accept) { g_osk->result = g_osk->text; std::fprintf(stderr, "OSK_RESULT %s%c", g_osk->result.c_str(), 10); }
  g_acts.clear();
  queue(kFnHidePage, 0);
}
void press(const Key& k) {
  switch (k.action) {
    case kChar: {
      const std::u16string& l = k.label[g_layer];
      if (!l.empty()) typeChar(char(l[0]));
      if (g_layer == kUpper) { g_layer = kLower; refreshKeys(); }   // one-shot shift
      break;
    }
    case kSpace: typeChar(' '); break;
    case kBackspace: if (!g_osk->text.empty()) { g_osk->text.pop_back(); refreshText(); } break;
    case kShift: g_layer = g_layer == kUpper ? kLower : kUpper; refreshKeys(); break;
    case kSymbols: g_layer = g_layer == kSym ? kLower : kSym; refreshKeys(); break;
    case kCancel: beginClose(false); break;
    case kOk: beginClose(true); break;
  }
}

}  // namespace

void Osk::attach(RunHooks& hooks) {
  g_osk = this;
  if (hooks.entry_n < 16) hooks.entry[hooks.entry_n++] = EntryHook{0x8049DFC4u, patchRect, nullptr};
  if (hooks.entry_n < 16) hooks.entry[hooks.entry_n++] = EntryHook{0x804DC584u, textGetter, nullptr};
  if (hooks.entry_n < 16) hooks.entry[hooks.entry_n++] = EntryHook{kFnSelectPage, blockOtherPages, nullptr};
  if (hooks.entry_n < 16) hooks.entry[hooks.entry_n++] = EntryHook{0x804C37A0u, selectDone, nullptr};
}

bool Osk::onFunction(const std::string& f) {
  if (f == "OSK") {
    if (!active && !pendingOpen) { pendingOpen = true; text.clear(); accepted = false; }
    else if (active && !closing) beginClose(false);
    return true;
  }
  if (!active && !pendingOpen) return false;
  if (!active || closing || g_keys.empty()) return true;      // opening or closing: swallow keys
  if (f == "UP") moveFocus(0, -1);
  else if (f == "DOWN") moveFocus(0, 1);
  else if (f == "LEFT") moveFocus(-1, 0);
  else if (f == "RIGHT") moveFocus(1, 0);
  else if (f == "OK" || f == "OSK_PRESS") press(g_keys[g_focusKey]);
  else if (f == "EXIT") beginClose(false);
  else if (f == "MENU") { g_layer = g_layer == kUpper ? kLower : kUpper; refreshKeys(); }
  else if (f == "OSK_ENTER") beginClose(true);
  else if (f == "OSK_BACKSPACE") { if (!text.empty()) { text.pop_back(); refreshText(); } }
  else if (f.rfind("OSK_CHAR:", 0) == 0 && f.size() > 9) typeChar(f[9]);
  else if (f.rfind("DIGIT_", 0) == 0 && f.size() == 7) typeChar(f[6]);
  return true;   // everything else is swallowed while the keyboard is up
}

void Osk::service(CPU& c, uint64_t dispatches) {
  static uint32_t peekAddr = 0, peekLen = 4; static std::string peekLast; static bool peekInit = false;
  if (!peekInit) { peekInit = true; if (const char* e = std::getenv("MANTA_PEEK")) { char* end = nullptr; peekAddr = uint32_t(std::strtoul(e, &end, 16)); if (*end == ':') peekLen = uint32_t(std::strtoul(end + 1, nullptr, 16)); } }
  if (peekAddr) {
    std::string now(reinterpret_cast<const char*>(P(c, peekAddr)), peekLen);
    if (now != peekLast) {
      std::fprintf(stderr, "PEEK %08x at dispatch %llu:", peekAddr, (unsigned long long)dispatches);
      for (uint32_t i = 0; i < peekLen; ++i) std::fprintf(stderr, "%s%02x", (i & 7) == 0 ? " | " : " ", (unsigned char)now[i]);
      std::fprintf(stderr, "%c", 10);
      peekLast = now;
    }
  }
  if (testAt && !testDone && dispatches >= testAt) { testDone = true; onFunction("OSK"); }
  if (activeFlag) activeFlag->store(active || pendingOpen);
  if (c.injecting || c.inject.pending) return;

  const uint32_t mode = R32(c, kZuiMode);
  if (closing) {
    if (!g_acts.empty()) { Act a = g_acts.front(); g_acts.pop_front(); c.inject.target = a.target; c.inject.a0 = a.a0; c.inject.a1 = a.a1; c.inject.pending = true; return; }
    if (g_closingAt == 0) g_closingAt = dispatches;
    if (mode == 1 || dispatches > g_closingAt + 30000000) {   // the page is hidden: give slot 17 back
      release(c);
      active = false; closing = false; pendingOpen = false; hideSent = false; g_closingAt = 0; g_acts.clear();
      std::fprintf(stderr, "osk: closed at dispatch %llu%c", (unsigned long long)dispatches, 10);
      if (activeFlag) activeFlag->store(false);
    }
    return;
  }
  if (active) {
    if (g_needFocus && dispatches > g_openedAt + 200000) {
      // The original page-enter code of slot 17 (804D975C...) hides all windows and shows only the ones of the channel banner; show every window of the keyboard.
      g_needFocus = false;
      std::fprintf(stderr, "osk: showing %u windows and focusing key %u at dispatch %llu%c", unsigned(g_windows.size()), g_focusKey, (unsigned long long)dispatches, 10);
      for (unsigned k = 1; k < g_windows.size(); ++k) queue(kFnShow, k, 1);
      setFocusKey(g_focusKey);
    }
    if (!g_acts.empty()) { Act a = g_acts.front(); g_acts.pop_front(); c.inject.target = a.target; c.inject.a0 = a.a0; c.inject.a1 = a.a1; c.inject.pending = true; }
    return;
  }
  if (!pendingOpen) return;
  // The firmware only opens a page while the surface pool has a free slot: its surface setup (8049DFC4) hangs on purpose otherwise. The pool manager (struct at
  // *806D87F8) keeps its slot table at M[0]; slot 5 (offset 56, state byte +4 == 0xFF when free) is the one a page surface takes. The page that is open releases it
  // some time after it is hidden, so: ask the open page to hide, wait until the ZUI is idle (mode 1) and the slot is free, exactly like the firmware's own key-driven
  // page switch, then select ours.
  const uint32_t pool = R32(c, R32(c, kSurfacePool));
  const bool slotFree = *P(c, pool + 56 + 4) == 0xff;
  if (g_selectBusy) return;   // never interleave with a page creation that is still running
  if (mode == 1 && slotFree) {
    buildPage();
    takeover(c);
    active = true; g_openedAt = dispatches; g_needFocus = true; g_acts.clear();
    std::fprintf(stderr, "osk: opening keyboard page (%u windows) at dispatch %llu%c", unsigned(g_windows.size()), (unsigned long long)dispatches, 10);
    c.inject.target = kFnSelectPage; c.inject.a0 = kSlot; c.inject.pending = true;
  } else if (mode != 1 && (R32(c, 0x806D4F9Cu) & 1) && (!hideSent || dispatches > g_hideAt + 5000000)) {
    // Only hide while the open page's surface exists (bit 0 of the surface flag 806D4F9C): the firmware's hide (804C3574) releases the surface through 8049DF04,
    // which does nothing when that bit is clear (the page is in the middle of being created or torn down).
    hideSent = true; g_hideAt = dispatches;
    std::fprintf(stderr, "osk: hiding the current page (ZUI mode %u, page %u) at dispatch %llu%c", mode, R32(c, 0x806D7E9Cu), (unsigned long long)dispatches, 10);
    c.inject.target = kFnHidePage; c.inject.a0 = 0; c.inject.pending = true;
  }
}

}  // namespace osk
