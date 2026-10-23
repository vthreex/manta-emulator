# On-screen keyboard (EXPLORATORY, native CPU build only)

Press **K** (Windows window or the Linux/ISO presenter) to open a keyboard that is drawn by the **original firmware ZUI**: a page of windows, rectangles and
draw styles written into guest RAM in the firmware's own data formats, painted by the original window/paint code and the original text renderer, selected and
opened through the original page-select (`804C3684`) and page-open code. No pixel is drawn by host code.

This is a new feature, not original behaviour: it is opt-out only in the sense that nothing happens until K is pressed, and it is labelled as such everywhere
(this document, the `osk:` stderr lines). The firmware file is not changed; guest RAM is modified at run time.

## Keys
| Key | Effect while the keyboard is open |
| --- | --- |
| arrows | move the highlight (original focus style) |
| OK / Enter | press the highlighted key |
| MENU / Tab | shift (one-shot) |
| EXIT / Esc | close without a result |
| PC keyboard letters, digits, punctuation, Backspace (Linux presenter) | type directly |
| `OK` button | close and accept: the text is printed as `OSK_RESULT <text>` on stderr/serial and kept in `Osk::result` |

While the keyboard is open no key reaches the firmware (remote filter) and no other page can be selected.

## How it works (formats recovered from the original code)
* **Page descriptor** `0x80686030 + 48*id`: `+0` window count, `+4` pointer to the **20-byte window records** (`+0` class: 0 root, 1 plain, 3 text, `+8` flags: bit0 visible,
  bit2 disabled), `+0x14..` page rectangle. Only ids 1..29 exist; every per-page dispatch table in the firmware has 30 entries.
* **Rectangle records** (`0x80695780[id]`, 12 bytes): `u16 0, u16 last-descendant index, x, y, w, h`; the root's index is the last window index.
* **Style lists** (`0x806A0178[id]`, 12 bytes per window): three pointers (normal, focus, disabled) to `u16` lists; a component is `type | index << 5`:
  type 2 = fill row (`0x80687670`, 12 B), 3 = text style row (`0x806877CC`, 12 B: font, colour, alignment), 4 = bitmap, 5 = rounded panel (`0x8068AA30`, 24 B).
* **Text**: the page's text getter (`804DC584` for page 17) returns a **pointer to NUL-terminated UTF-16LE text**; the style row comes from the window's type-3 component.
* **Focus** is `SetFocus(hwnd)` (`8049FF00`, state at `0x80674204`), visibility is `ShowWindow(hwnd, 1/0)` (`804A0460`).
* **Surface pool**: the page surface is allocated by `8049DFC4`, which hangs on purpose (`j self`) when the pool has no free slot. The slot table is at `M[0]`
  (`M = *0x806D87F8`), slot 5 at offset 56 (state byte `+4 == 0xFF` when free). The keyboard only opens when the ZUI is idle (`0x806D7E98 == 1`), the slot is free
  and no page select is running.

## Host code (native/runtime/osk.cpp)
The keyboard takes over **page 17** (the channel-number banner) while it is open and gives it back on close (descriptor and the two array entries are saved and restored).
Interventions, all entry hooks of the native dispatcher:

| Hook | Why |
| --- | --- |
| `8049DFC4` | the original open function of page 17 hard-codes the page rectangle on the stack; the hook rewrites that rectangle, then the original runs |
| `804DC584` | the page's text getter returns the key labels |
| `804C3684` | while the keyboard is open every page select except page 17 is ignored (the firmware re-selects page 2 every few million instructions) |
| `804C37A0` | marks the end of a page select (never interleave with a page creation) |

Visible effects are original calls injected at run boundaries (`CPU::inject`: registers saved on the guest stack, call, restore): `SetFocus`, `ShowWindow`, repaint,
hide (`804C3574`), select (`804C3684`).

## Browser preparation
`Osk::result` / `Osk::accepted` hold the entered text; a later browser page only has to request the keyboard (`onFunction("OSK")`) and read the result.

## Page engine and the network page (native/runtime/pages.cpp, page_osk.cpp, page_net.cpp)
The keyboard code was turned into a small page engine: a page is a `PageDef` (windows, labels, focusable items, callbacks); `Host` opens one page at a time through the
mechanism above, moves focus between items (nearest in the pressed direction), hands keys to the page, and can switch pages (the keyboard returns to the page that asked for
it). Text windows must be at least one font line (~36 px) high or the original renderer draws nothing.

* **N** opens the **Network** page: interface, address, mask, gateway, DNS and MAC (from `/run/net.txt`, written by `/netup`), a result box and four buttons:
  *Ping host...* (opens the keyboard prefilled with the last host, then pings it), *Ping gateway*, *Refresh*, *Close*.
* Ping = 4 ICMP echoes over a raw socket on a worker thread (`native/runtime/net_ping.h`), name resolution through the system resolver.
* `/netup` (native/iso/netup.c, started by `/init` in the background): loads the NIC driver modules matching the kernel (native/iso/stage_modules.py takes them from Tiny Core's
  `corepure64.gz`: e1000, e1000e, igb, r8169, 8139, virtio_net, vmxnet3, tg3, several USB NICs), waits for an interface, DHCP, `/etc/resolv.conf`.
* Verified in QEMU only (user-mode networking, e1000): DHCP lease, gateway ping, DNS, external ping. Real NICs and Wi-Fi are untested / unsupported (no Wi-Fi drivers or firmware).
