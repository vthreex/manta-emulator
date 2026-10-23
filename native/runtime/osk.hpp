// On-screen keyboard (EXPLORATORY feature, native CPU only).
//
// The keyboard is a page of the ORIGINAL firmware ZUI: its windows, rectangles and draw-style lists are data in guest RAM in the firmware's own formats
// (descriptor at 0x80686030 + 48*id, 20-byte window records, 12-byte rectangle and style-list records), drawn by the original window/paint code and its
// original text renderer through the original page-select and page-open functions. The page takes over the slot of ZUI page 17 (the channel-number banner) while
// it is open and gives it back afterwards. Nothing in the firmware file is changed; the only code-level interventions are three entry hooks (see osk.cpp).
// Key events never reach the firmware while the keyboard is open: they are consumed by the host (remote filter) and turned into original ZUI calls
// (SetFocus, repaint). See docs/osk.md.
#pragma once
#include "rt.hpp"
#include <atomic>
#include <cstdint>
#include <deque>
#include <string>

namespace osk {
struct Osk {
  // ---- state ----
  bool pendingOpen = false;   // open requested; waits until the ZUI is idle and the surface pool has a free slot
  bool active = false;        // the page owns slot 17 (open or opening)
  bool closing = false;       // hide requested, waiting for the ZUI to become idle before slot 17 is given back
  bool hideSent = false;      // the current page was asked to hide so that ours can open
  uint64_t testAt = 0;        // diagnostic: request the keyboard once when the dispatch counter reaches this (0 = off)
  bool testDone = false;
  std::string text;           // the typed text (ASCII)
  std::string result;         // text of the last accepted entry
  bool accepted = false;
  std::atomic<bool>* activeFlag = nullptr;   // mirrored for the presenter (it maps letters to characters while the keyboard is open)

  void attach(RunHooks& hooks);                          // register the entry hooks
  void service(CPU& c, uint64_t dispatches);             // called between runs: opens/closes the page, runs queued ZUI actions
  bool onFunction(const std::string& function);          // remote filter: true = consumed by the keyboard (never delivered to the firmware)
};
}  // namespace osk
