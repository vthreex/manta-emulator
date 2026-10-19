# Remote-control input path (original firmware) and the virtual remote

## What the firmware does (static + traced)
- The PM-domain IR FIFO region (0x1F007B00..) is **polled**, not interrupt driven. A frame is the *raw command byte* (logical IR_KEY 3DA9, byte 7B51);
  the repeat flag is 3DAD bit 0 and is read after the pop (hypothesis: it latches the popped entry's flag — `--exploratory-pm-ir-rpt-latch`).
- raw byte -> application key: jump table at `0x8061773C` (index < 0xFD); every target stores a constant into `0x806B910C`.
  The extracted table is `docs/ir-raw-to-app-key-table.tsv` (84 distinct app codes). It is *firmware evidence for the translation only*.
- app code -> user function (MENU, SOURCE, arrows...) is **not yet established**. `configs/input/ir-keymap.tsv` therefore stays empty:
  no raw byte is invented. Until evidence exists, the window sends *named functions* that have no mapping and are logged as "no raw IR byte is mapped".

## Virtual remote (host side, no direct firmware calls)
- `src/input/remote_queue.hpp`: thread-safe queue of function names, `IrKeyMap` (FUNCTION RAW [evidence]), `parseIrScript`.
- The emulator thread drains the queue every 0x4000 dispatches and calls `Riu::receivePmIrDecoded`, i.e. it only fills the emulated PM IR FIFO.
  Events the receiver cannot take yet wait (never dropped). Every delivery is in the summary field `virtual_remote_log` and in an event.
- Deterministic replay: `--ir-script "DISPATCH:FUNCTION,DISPATCH:raw=0xNN"` (dispatch ordered). Raw literals are for experiments only.
- Windows window: M, S, Esc, Enter, arrows, P, I are host functions; `O` stays the offscreen inspector.

## Result: the original key reader consumes injected frames (evidence/virtual-remote-original-key-path.json)
In run s14 two scripted raw bytes (0x00 at dispatch 1.300B, 0x01 at 1.305B) were popped from the emulated PM IR FIFO (pops=2) and the original key code at `8046A754`/`8046AA70`
ran right after each. Raw `0x01` made the ZUI create a new 1320x120 window and request a GE stretch BitBlt (65x53 -> 120x100, ratio 8AA/947) that is not modelled yet, so the run stops there.
Which app code is MENU/SOURCE/EXIT/OK/arrows is still unknown; the keymap file stays empty.

## Open
- Identify app codes of MENU/SOURCE/EXIT/OK/arrows by experiment (scripted raw injection + watch of `8046AA70`) and record each with evidence.
