// Custom pages in the ORIGINAL firmware ZUI (EXPLORATORY features, native CPU only): the on-screen keyboard, the network tool and the browser.
//
// A page is a table of windows, rectangles and draw-style lists in guest RAM in the firmware's own formats (see docs/osk.md). The engine takes over the slot of ZUI
// page 17 (the channel-number banner) while a page is open and gives it back when the last page is closed; the original page-select, page-open, window-paint,
// text-render, focus and show code does all drawing. Key events never reach the firmware while a page is open: the remote filter consumes them.
#pragma once
#include "rt.hpp"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pages {

// draw-style components: type in the low 5 bits, table index above (firmware 8049E7BC); all of them are rows of the firmware's own tables
constexpr uint16_t comp(unsigned type, unsigned index) { return uint16_t(type | (index << 5)); }
constexpr uint16_t kFillDark = comp(2, 1), kFillBlack = comp(2, 0), kFillHighlight = comp(2, 4), kPanelTeal = comp(5, 2);
constexpr unsigned kTextCentre = 210, kTextCentreDark = 211, kTextCentreGrey = 212, kTextLeft = 4, kTextLeftDark = 5, kTextLeftGrey = 6;   // type-3 rows (font, colour, alignment)

struct Win {
  uint32_t cls = 1, flags = 0x83;                   // class 0 root, 1 plain, 3 text; flags: bit0 visible, bit2 disabled
  uint16_t x = 0, y = 0, w = 0, h = 0;              // relative to the page (the root: screen position and size)
  std::vector<uint16_t> normal, focus, disabled;    // draw-style component lists
};

struct Item { unsigned hwnd = 0; unsigned row = 0; int cx = 0; };   // a focusable window

class Host;
struct PageDef {
  std::string name;
  std::vector<Win> windows;                         // [0] is the root
  std::vector<std::u16string> labels;               // text of window k
  std::vector<Item> items;
  unsigned focus = 0;                               // index into items
  std::function<void(Host&, unsigned item)> onPress;                  // OK on a focusable window
  std::function<bool(Host&, const std::string& function)> onKey;      // called first for every key; true = consumed
  std::function<void(Host&)> onService;                               // called between runs while the page is open (live updates)
  std::function<void(Host&)> onClosed;                                // called after the page was hidden
  unsigned add(Win w, std::u16string label = {}) { windows.push_back(std::move(w)); labels.push_back(std::move(label)); return unsigned(windows.size() - 1); }
};
using PagePtr = std::shared_ptr<PageDef>;

class Host {
public:
  std::atomic<bool>* activeFlag = nullptr;                // mirrored for the presenter (it sends characters instead of remote functions while a page is open)

  void attach(RunHooks& hooks);                           // registers the dispatcher entry hooks
  void service(CPU& c, uint64_t dispatches);              // called between runs
  bool onFunction(const std::string& function);           // remote filter: true = consumed (never reaches the firmware)

  // ---- page control (call from pages' callbacks / launchers) ----
  void request(PagePtr page);                             // show `page` (the page that is open is hidden first)
  void closeAll();                                        // hide the open page; nothing follows
  void setLabel(unsigned hwnd, const std::u16string& text);   // new text for a window of the open page (repainted)
  void setFocus(unsigned item);                           // focus a focusable window of the open page
  void showWindow(unsigned hwnd, bool show);
  bool open() const { return state_ == kOpen; }
  PageDef* page() const { return current_.get(); }
  uint64_t dispatches() const { return now_; }

  // diagnostic: request the on-screen keyboard once when the dispatch counter reaches this value (0 = off)
  uint64_t testOskAt = 0;

private:
  enum State { kIdle, kPending, kOpening, kOpen, kClosing };
  struct Act { uint32_t target, a0, a1; };
  State state_ = kIdle;
  PagePtr current_, next_;
  std::deque<Act> acts_;
  uint64_t now_ = 0, openedAt_ = 0, closingAt_ = 0, hideAt_ = 0;
  bool needInit_ = false, hideSent_ = false, testDone_ = false;
  struct Saved { bool valid = false; uint32_t count = 0, table = 0, styles = 0, rects = 0; } saved_;

  void queue(uint32_t target, uint32_t a0, uint32_t a1 = 0) { acts_.push_back(Act{target, a0, a1}); }
  void takeover(CPU& c);
  void release(CPU& c);
  void beginClose();
  bool runAct(CPU& c);
  friend struct HostHooks;
};

// the page factories (page_osk.cpp, page_net.cpp)
void requestKeyboard(Host& host, const std::string& initial, std::function<void(bool accepted, const std::string& text)> done, PagePtr returnTo = nullptr);
void requestNetworkPage(Host& host);
void requestBrowserPage(Host& host);

}  // namespace pages
