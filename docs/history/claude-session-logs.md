# Claude session logs 2026-10-18 .. 2026-11-04 (archived 2026-11-12)

Moved from CLAUDE.md unchanged; later work is in git history and docs/STATUS.md.

## Session log (Claude)
- `f59cf26`: `src/video/ge_stretch_request.hpp` + `docs/ge-8040-register-spec.md`. Decode-only; the recorded 52-write
  `8040` request is replayed in core_tests and cross-checked against the original scale arithmetic. Command bit 15 =
  stretch, bit 14 = nearest, mode reg `01` bit 4 = stretch enable (original code + Marlon agree).
- Format-C alpha, stretch filter, dither pattern and key comparison point are NOT recoverable from the firmware
  (no matrix/tap registers are written). Needs a Titania4/Uranus-family `halGE.c`, vendor docs, or captured GE output.
  Public searches (GitHub/web) found only API-level headers. Do not guess pixels; any model must be opt-in and labeled.
- Build: existing ninja dir is `../../work/build` (add `work/deps/bin` to PATH, `ninja core_tests`).
- Ownership study (docs/avd-register-ownership.md, tools/avd_register_ownership.py): `11DB`, `11FC`, `113C/D` have no
  software writer anywhere (AVD fw, original MIPS, init table, Marlon/Amethyst). They are silicon-owned; `11DB.7` is not
  the real gate (both outcomes stop at `0993` needing `11FC.2`). Mapping: BF206A00+(xx>>1)*4+(xx&1) = AFEC bank 1035 bytes.
  Possible (unimplemented) strict-mode design: unknown-effect join at `80501BFC`. Needs user decision.
- Windows note: `$TEMP/mdis.py`-style MIPS disassembly needs `pip install capstone`; do not name scripts `dis.py`.

## Session 2 log (Claude, 2026-10-18)
- Corrected ownership study (759991f): AVD *does* write 11DA/11FA/11FC; helper-call writes (`02AA`,`076E`) were missed by the first scan.
  Mailbox 11DC..DF decoded = `HAL_AVD_AFEC_SetRegFromDSP` (MIPS 8050328C). `tests/avd_ownership_regression.py` anchors the scanner (5 known accessors).
- Static join proof for MIPS `80501BFC` (tools/mips_join_proof.py): outcomes differ only in dead v0/v1/a0 + RAM `806D83D4`,`806AD960`.
- New opt-in diagnostics (all labelled, guarded by tests/diagnostic_options_regression.py): `--diagnostic-htotal-branch-join`,
  `--exploratory-avd-no-sync-events`, `--diagnostic-avd-invalid-publication`, `--diagnostic-avd-11db-bit7 N` (bracket 0 AND 1).
- VD/AVD MCU abstract model extended soundly: INC/DEC A, CPL A on partially known A, AJMP. (`src/cpu/vd_mcu.cpp`)
- Probe flags: `--clear-only-event-ack`, `--invalid-publication`, `--synthetic-xdata ADDR VAL MASK`; `tools/avd_frontier_explore.py`.
- Windows: a running manta.exe locks the file; copy to another name (manta_d.exe) before launching long runs.
- Cold run ≈ 8 min (`work/ge-blit-cold-command.json`, drop `--record-trace`); trace for replay: D:/CodexWork/.../ge-blit-cold.jsonl.
- Commit 39e8530: diagnostics committed. Diagnostic run (join + AVD hypotheses + generated HAL grants + hash-bound Set3dCombSpeed
  paired restore) reaches 855,202,172 MIPS dispatches (+407,001), still inside platform polling (8046CA74 never returns).
  NEXT: stop at 8053C9A8 reads BF0011C8 = ISP reg 0x1C8 inside flash-write routine 8053C914 (called from ~803E7AD0). Needs SPI NOR
  program/erase + register 1C8 model (standard W25Q64BV commands are documented; 1C8 is not). Then UI wrapper 8047602C / dispatcher 804E0DC4.
- tests/pm_power_boot_regression.py fails (pm51_state_sha256) on the cc225d7 binary too: pre-existing, not investigated.

## Session 3 log (Claude, 2026-10-18 evening)
- `--candidate-spi-nor-write` (opt-in; off = strict boot byte-identical): ISP command-mode address phase (+08/+0C), WDATA buffer timing, SPI NOR WRSR / page program / erase,
  SEC=0 block-protect table, BUSY, copy-on-write `Flash::programAnd/eraseRange`, CPU-window mirroring. docs/spi-isp-nor-write.md, tests/spi_nor_program_tests.inc (1,806 native checks).
- `--diagnostic-restore-by-identity-branch`: 8053CB80 branch in the SerFlash page-write routine (both arms restore ISP 1C8 bit2 to its tested value).
- Original flash routine 8053C914 now runs to completion in a diagnostic run: 7 page programs, 1,330 bytes, 0 erases, 0 violations, 2 overlay sectors; MIPS 855,843,242 dispatches.
- NEXT stop: 803BD910 reads BF200E10 (byte 8, `andi 0x40`) in USB-state fn 8054E3C0 (called from the main flow, ra 80488C44). Platform polling 8046CA74 still not returned.
- Policies: configs/diagnostics/opaque-spi-flash-write.tsv (superset). Regression driver idea: copy manta.exe to manta_X.exe before long runs; suites list in evidence/spi-nor-write-validation.json.

## Session 4 log (Claude, 2026-10-18 night)
- a6db0cf validated (18 suites, strict replay identical, q0/q1 deterministic). ceb9475: GE format-C (ARGB1555_DST) fills for rule-independent colours
  (alpha 00/FF, black/white) + reg 0x59 bit 6 (GE-init default 0x40). 1,823 native checks; 18 suites + strict replay + r0 rerun pass (evidence/ge-format-c-fill-validation.json).
- Diagnostic connected run (q0 flags + manta_o): OSD 3 activated, dispatcher 804E0DC4 entered, 113 windows, 2 fills retired (674,560 px). 0x8040 bitmap 196 still guarded.
- NEW STOP: MIPS 80509A94 reads 0x1F224471 (VIF handler 8050962C, bit0 tested -> 80509E58). Not yet identified; do not fabricate.
- e30deb2: VIF `0x1F224470/71` = CR_FOE / CR_LOCK_STATUS (reference MsVIF offsets 0x38/0x39; `tools/riu_const_xref.py` finds loads only, no writer). `--diagnostic-vif-cr-lock N`
  (bracket 0/1; both pass 80509A94, same next stop). docs/vif-cr-lock-status.md.
- `--export-gop-frame FILE.json` (debug decode of enabled+latched GOP windows; format 6 ASSUMED ARGB1555; never an authentic-frame claim). The connected state has bank4/win1 at (0,520) 1360x248 @0xE82380,
  opaque black from fill 2 (fill 1 cleared the offscreen surface 0xDDD880). Caller chain 8047602C->80480CA8->804C3684->804AFDFC->8049DFC4(8049E184/8049E244)->803EAC58->805202C8->80522F20.
  docs/gop-scanout-export.md, evidence/connected-osd3-clear-and-gop-window.json.
- `--exploratory-avd-htotal-constant N` (needs absent-HSYNC; bracket 0/0xFFFF/0x360 identical): the second main-loop iteration passes; ZUI wrapper/dispatcher run 104 times; stops at the guarded
  stretch BitBlt command 8040 (4x33 strip -> 1360x68 at y=180 into the OFFSCREEN surface 0xDDD880; decode in the `ge_stretch_request_guarded` event). docs/avd-htotal-constant-hypothesis.md.
- Stop-reason strings of the guarded GE paths are an exact tested contract (zui_flow suite); carry extra detail in events, not in the exception text.
- Do not run `ninja` while a suite driver is executing probes (relink races); copy manta.exe to manta_X.exe for long runs.
- NEXT: unresolved format-C/filter/dither for the 4x33 strip and bitmap 196; the native window still shows a placeholder (no live presenter); TSP/OpenRISC reference suites remain stale (not regenerated).

## Session 5 log (Claude, 2026-10-19)
- Bitmap 146 = 4x38 uniform strip; glyph = "SCART" 2bpp MSB-first; exploratory models: uniform stretch, I2 glyph, blend copy (+ height5-full hypothesis), AFEC noise/indirect(6A3C)/`--exploratory-mmio-bits`.
- Presentation: FB1 0xDDD880 back, FB2 0xE82380 shown by GWIN5; flip 8049DD70 (from 804A2FF4) and dirty-copy 803EE398 (animation, 804EE128). All 32 copies execute; step 24 first SCART pixels in FB2; after step 32 ZUI disables GWIN5, flips, re-inits.
- Next stop: GE fill 0x30 into an I8 (format 4) 1024x658 surface at 0xDDD880 (decode in summary `ge_guarded_stretch_request`). Run flags: see scratchpad commands q0 + vif-cr-lock 0 + htotal 0 + noise 0 + afec-indirect + mmio-bits 1f206f5d:52:00 + policy opaque-afec-indirect-port.tsv.
- NEVER `git checkout` a file with uncommitted session edits (lost machine.hpp edits once; re-applied).

## Session 6 log (Claude, 2026-10-19/20)
- CORRECTION: the complete SCART bar WAS visible in enabled GWIN 5 (history frame at 870.05M == FB1). Flip = ctrl rewrite + base 0xDDD880 + vsync latches; GWIN5 disabled at 915.7M by the ZUI OSD close (804C3684(a0=2) from 80486778), then a full-screen 1360x768 black window at 0xFDB880.
- `--exploratory-ge-i8-fill` (format 4 = 1 byte/pixel per table 80525E28; index = low byte of regs 70/71), `--gop-timeline-from`, `--watch-pc-regs`, fill-refusal register dumps. Run reaches 969,234,885 dispatches (OSD 2 loop, 68 I8 fills), stops at scaler read BF205E79 (805FBBA4).
- Docs: docs/gwin5-handoff-page-flip-and-i8.md; evidence/gwin5-handoff-and-i8.json (d61b539).

## Session 7 log (Claude, 2026-10-19)
- Virtual remote built + committed (src/input/remote_queue.hpp, `--ir-keymap/--ir-script/--exploratory-pm-ir-rpt-latch`, window keys M/S/Esc/Enter/arrows/P/I). Delivery only fills the emulated PM IR FIFO; firmware polls it.
- raw->app table at 0x8061773C extracted to docs/ir-raw-to-app-key-table.tsv; app code -> function (MENU...) NOT identified yet, configs/input/ir-keymap.tsv intentionally empty. docs/ir-input-path.md.
- `--exploratory-mmio-bits ADDR:MASK:VALUE@SCBANK` scopes scaler-aperture hypotheses to a bank. BF205E79 family = sync status/period/total accessors (805FBB6C/805F2EA0): no-sync sentinels pass them.
- s7 run: 1,002,388,028 dispatches; stops at 80526598 on a GE fill with reg 07=8584 (non-plain flags). 804C3684(2,N) loop continues independently of scaler (N += 11).
- Suite args: firmware/display/clock/diagnostic_options/ge_*option: --firmware --emulator --report; zui_flow/vif_agc add --probe; immutable_input & window_preview take no --firmware.
- NEXT: scripted raw-byte experiments to map app codes -> UI transitions; decode GE flags reg 07=8584; NO SIGNAL search.
- Session 7b: `--exploratory-ge-fill-ignore-reg00 6` passes the reg00=0007 fill (original code sets bits 1,2 via 80520A30/80525B78; meaning unknown). Next stop: GE cmd 0x2030 (bit 13 + regs 74..7D FAAB/000F, probably gradient); see evidence/ge-reg00-flags-and-gradient-stop.json.
- NO SIGNAL = string 0x1A7 (text resource stream-31bf58, table L at 0x240+L*0x1300, data 0x1F040); chosen in ZUI text callback 804D95A0 from status word 806C92C0. docs/no-signal-osd-findings.md. Writer of NO SIGNAL values not found yet.
- Run helper: scratchpad/mkrun.py (rebuilds a command JSON with a new binary/summary tag); the s7-s10 commands are lists, not dicts.
- Session 7c: **NO SIGNAL dialog drawn by the original ZUI** at s12 (1,305,936,592 dispatches) — flags = s7 base + i8 fill + fill-ignore-reg00 6 + gradient-fill. evidence/no-signal-dialog-*.{json,png}. Next stop: scaler poll BF204A3C bit12 at 805FAFD8 (retry 0x20 x sleep 805CBBD8), then BF204C14&0xC0.
  Gradient/format-C quantization are labelled ASSUMPTIONS; nothing verified as TV pixels. Next: pass the scaler poll with a bracketed bit hypothesis, then key handling (MENU) — raw->app table in docs/ir-raw-to-app-key-table.tsv.
- Session 7d: s13 (s12 + `--exploratory-mmio-bits 1f204a3d:10:10`) runs to the 1.6B budget (NO SIGNAL dialog relocates = firmware behaviour). s14 injected raw bytes via `--ir-script`: firmware popped them (pops=2) and the key reader 8046A754/AA70 ran;
  raw 0x01 (app 0x11) -> new 1320x120 window + GE BitBlt 8040 65x53->120x100 fmt 9->C (filter/blend unresolved) = NEXT STOP. evidence/virtual-remote-original-key-path.json. Keymap still empty (no proven MENU/arrows).
  Helper scripts: scratchpad run14.py + sN-extra.json (extra args), mkrun.py.

## Session 8 log (Claude, 2026-10-19 evening)
- CORRECTION: the raw->app table of f218787 was wrong (read the last immediate of an 8-instr window). Correct table: docs/ir-raw-to-app-key-table.tsv from tools/ir_key_table_extract.py (73 stub-exact entries; 172 raws share the default target 8046AA64 = no key).
- CORRECTION: `lui 806C; lw -0x6D40` is **0x806B92C0** (not 806C92C0); `-0x6FA4` = 806B905C; `0x8A8(lui 806C)` = 806C08A8. Negative offsets from lui 0x806C subtract from 0x806C0000.
- Digits proven: app 0x10..0x19 = 0..9 (8046DA24 numeric entry); raw 1..9 -> digits 1..9, raw 0x45 -> 0. configs/input/ir-keymap.tsv has only these; window maps '0'..'9'. MENU/SOURCE/EXIT/OK/arrows still NOT identified.
- Bitmap 142 (65x58 ARGB1555 antenna icon) = source of the 0x8040 stretch; OSD 17 = (1020,20,300,100) digit banner. `--exploratory-ge-nearest-stretch` (needs uniform-stretch opaque-source; phase = half-pixel term) executed 8 stretches. docs/ge-format9-stretch-bitmap142.md.
- Parallel single-key runs: 8 emulators at once is fine (16 cores) BUT the processes hang at exit after writing the summary: scratchpad/keybatch.py kills them once the summary is stable. Boot to 1.3B ~15 min CPU.
- In the connected NO SIGNAL state keys are consumed by the early key chain; no key is forwarded to the ZUI (803FFE40 never hit); OSDs shown by keys: 14, 15, 3 only (see evidence/key-sweep-no-signal-state.json).
- NO SIGNAL text: callback 804D95A0 (event 4) calls 8049E548(0x1A7) ~98 times at >=1.0024B dispatches (verified by watch run).
- Session 8b (menu): original MENU opens! Keys (behavioural, see docs/ir-key-semantics.md + configs/input/ir-keymap.tsv): MENU 0xFC, UP 0x0B, DOWN 0x0E, LEFT 0x10, RIGHT 0x11, OK 0x0D; EXIT cand 0x20, INFO cand 0x16, MUTE 0x0F, SLEEP 0x17.
  Needs flags: s13 base + `--exploratory-ge-nearest-stretch` (+keyed K1, pitch clamp P1) + `--exploratory-ge-copy-snapshot` + `--exploratory-pm-ir-rpt-latch`. Real WM_KEYDOWN test: tools/live_window_keys.py (evidence/live-window-keys.json).
  Runs ~15 CPU min to 1.3B; 7-8 parallel is OK but each wave then takes 25-30 min; the machine sleeping kills background runs.
  Strict replay (6,592,741 events) identical; pm_power_boot_regression PASSES now. Open: SOURCE/POWER/INFO naming, GPIO read 0x1F001CD0 (804F130C) after power-like key, MMIO read 805E3508, many GE hypotheses unverified.

## Session 9 log (Claude, 2026-10-20)
- Full-machine save states (docs/save-state.md): `--save-state/--load-state[-allow-extra-options]/--state-digest`; schema 1, per-section + overall SHA-256, atomic write, fingerprinted options.
  Interactive NO SIGNAL checkpoint loads in ~1.1 s (cold boot ~15 min). Equivalence continuous-vs-restored is exact (summary + per-section digests). `tests/snapshot_regression.py`.
  Checkpoint files live in `work/checkpoints/` (not committed): regenerate with `configs/checkpoints/connected-menu-base.json` + `--save-state`. Re-save old snapshots (no option lines) before using the superset flag.
- `tools/checkpoint_sweep.py`: one raw IR byte per short run from a snapshot (no 256 cold boots). SOURCE: no cycling key; direct source keys call 8048710C (docs/ir-source-power.md). POWER candidate raw 0x0A -> 8040468C;
  `--exploratory-mmio-bits` now also accepts the PM byte 1F001CD0 (carried-back stores allowed); both bit-6 outcomes stop at the PM RMW helper reading 0x1F002030 (unmodeled).
- Speed: `--profile FILE` sampler; `PreservedBits`/`UnknownObservations` live-register fast paths, page filter for unknown RAM bytes, allocation-free `hex()`/`event()` line building. Output byte-identical (digests + strict replay). ~32% less wall time on the heavy resumed segment.
- Shutdown: headless exits right after the summary (tests/shutdown_regression.py); a windowed run without `--preview-close-after-ms` keeps the last frame open BY DESIGN. The old "hang" did not reproduce.
- Do not run `ninja` while the suite driver is running; copy manta.exe to manta_X.exe for long runs. `$TEMP/final_suites.py`-style driver: firmware/display/etc need `--firmware --emulator --report`; zui_flow/vif_agc/ge_clear need `--probe`.

## Session 10 log (Claude, 2026-10-24..27)
- Browser stages: forms/cookies/POST/meta refresh/fragments (native/runtime/page_browser.cpp, docs/browser.md); QEMU driver work/iso/qdrive.py.
- **Qt control centre** `frontend/manta_control.py` (PySide6; docs/frontend.md, docs/frontend-protocol.md): emulator `--frontend-shm NAME` (named shared memory frames, stdin KEY/RAW/PAUSE/RESUME/QUIT),
  `--preview-max-fps`, `--miu1-mb` (RAM bank), live-preview change detection (`publishPreviewLive`). Remote model with key/mouse modes; machine settings; PyInstaller build script. tests/frontend_protocol_regression.py.
- Speed: VdMcuCpu::Impl::boundary used decode() (sprintf) per 8051 instruction -> length table: 6.4 -> 12.2 M dispatches/s headless, summaries identical (cpi 2: 39 M/s). tools/host_profile.py = host sampling profiler.
- Keys: `tools/key_sweep_native.py` + `tools/make_dtv_state.py`; DTV-state sweep gave LANGUAGE 0x19, LIST 0x1D, FAV 0x57, EPG 0x4A/0x54, SUBTITLE 0x5B/0x71/0xF4, TTX 0x12 (evidence/key-sweep-dtv). VOL/CH/INDEX/ZOOM/REVEAL/HOLD/RADIO/3D/L-R/REPEAT still unproven.
  Native engine from the DTV-hypothesis state stops on an unmodeled MMIO read (interpreter passes): native/interpreter divergence not investigated.
- Windows: ASCII-only tool arguments; Python edit scripts must be written with the Write tool (backslash/newline escapes in Bash heredocs are mangled); several source files are CRLF.

## Session 11 log (Claude, 2026-10-27, performance)
- Goal from the user: stable 60 fps and a cold boot in 30 s via more MIPS/s or recompilation. Switch/PS5-style HLE is rejected (per-image, would fake firmware behaviour); generic recompilation + EXACT idle skipping is the path.
- Measured: native recompiled CPU alone ~585 M instr/s; the machine is bound by peripheral models. Interpreter cold phase ~8 M/s (75% per-instruction hooks); native from the 200 M handoff ~38 M/s.
- 572127c: `Riu::advanceTsp` exact idle-loop fast-forward for the TSP OpenRISC core (docs in code; `MANTA_NO_OR_SKIP=1` disables; off with --trace/--replay/--pause-mmio). Proof per loop: identical Arch state after one iteration,
  every byte written restored, two consecutive identical windows, external reads re-verified before each skip, log-filter counters credited exactly. Connected boot: 600 M instr 114.5 s -> 24.2 s native (4.7x), 130 M interpreter 26.1 -> 4.7 s,
  cold 330 M 39-42 s -> 17 s. tests/or_idle_skip_regression.py (335 summary fields + all state digests equal). core_tests 2048.
- Remaining after the skip (native 200 M -> 1.3 B = 29 s): ~5.8 M advanceTsp bursts (one per MMIO access; each re-derives the 8-register TSP mapping via knownBits/get, runs ~40 steps to/from the loop head, re-verifies ~11 deps).
  Idea not done: lazy advance only at TSP-visible accesses (native engine only; interpreter must keep eager event credit because of sideEffectGen_ loop skipping).
- Control-centre configuration (atv-boot.snap, ATV picture, AVD model, preview 60 fps) runs only 12.7 M instr/s: ~40% is the AVD 8051 (`VdMcuCpu::Impl::boundary` 22%, codeTableData, tick), ~17% preview compose/copy (composeGopPreview, wmemmove, collectGopLayers).
  NEXT: exact idle-skip for the AVD 8051 (needs provenance state: registerKnown/Origin, carry/parity known, timers) and cheaper preview (skip unchanged layers / avoid copies); interpreter hooks (codeHookFull/recordPc/pushRecent) for the cold phase.
- Handoff to native at the runtime entry (~100-120 M) stops with "GOP palette command contains unknown color bits" (native lacks the interpreter's unknown-bit provenance); snapshots avoid this (early_200000000.snap works).
- Tooling quirk: Bash heredocs turn backslash-n inside Python/C++ strings into real newlines; use the Edit/Write tools for any text containing escapes. riu.cpp/riu.hpp are CRLF.

## Session 12 log (Claude, 2026-11-04)
- Phantom IR key fixed (src/peripherals/riu.cpp zero-unknown path): `--diagnostic-riu-zero-unknown-reads` zeroed the PM IR FIFO EMPTY bit (1F007B59 bit1), so every 108 ms poll
  (804F2284/804F2474) popped raw 00 (app 0x64) -> any open page closed (menu handler default 804CA08C -> 804F0724(0x17,5)). Now menus/lock/sub-pages stay open; no hack needed.
- MENU is raw **0x40** (toggle/return; handler 8046AE60). 0xFC only opens the menu (now MENU_OPEN). Keymap, live page LIVE_RAW and docs/ir-key-semantics.md updated.
- `--exploratory-menu-no-autoexit` now off by default (frontend menu_stays_open=False); its redirect gained a key pass-through (rt.hpp key_pc/last_key).
- Live page frames are padded to the full 1360x768 panel (video-plane colour or black outside the GOP extent) in manta_frame_update.
- Disassembly helper: scratchpad mdisasm.py pattern = tools/mips_join_proof.load_image + capstone (`python mdisasm.py START-END ...`).
- USB pendrive hot plug: `usb-insert=IMAGE` remote function (machine.cpp insertUsbImage/wireUsbBus, UsbHost::adopt); live page "USB pendrive" box builds FAT32
  in JS (site/live/pendrive.js) and the worker hot-plugs it. SOURCE -> USB -> OK shows the original media player; enumeration takes ~250 M instructions.
  The source list closes on its own after ~2 s firmware time, so press DOWN quickly. docs/usb-pendrive.md.
- USB software update works natively (38e2c3d): SOURCE->ATV, MENU, CHANNEL tab "Software Update(USB)", OK, LEFT. MERGE.BIN must be <=0x7E0000 bytes with a trailing
  big-endian CRC32 (MSB-first, 04C11DB7, seed 0 -> whole-file CRC 0). Needed: --exploratory-bdma-crc-miu, BDMA 0x3030 in --exploratory-bdma-mem-copy, SpiIsp closes a lone WREN
  before 3333 data mode (erase routine 8053CE50). Firmware log goes to stdout after "UART:" in native runs. docs/usb-pendrive.md.
- **Native cold boot** (docs/native-cold-boot.md): native/tools/gen_boot.py recompiles boot ROM (BFC00000 + 9FC00000 alias) and stage 2 (81000000) into native/gen
  (re-run cmake); `--native-cpu --native-cold-boot` without --load-state boots from the reset vector to NO SIGNAL (desktop 50 s / web 88 s). Fixes: MMIO clock sync inside runs,
  CP0 Cause IV/DC write bug, native-cold palette relaxation, code-area guard. Live page: firmware chip reset or "Reboot" -> manta_save_flash overlay -> cold boot with --nvram.
- **HDMI input** (docs/hdmi-input-model.md): `--exploratory-hdmi-signal MODE` (src/video/hdmi_modes.hpp, CEA/VESA timings, XTAL 12 MHz) answers the scaler IP1 mode detector
  (bank 1 regs 0x1E/0x1F/0x20 + window 0x12..0x15) only while bank 1 reg 0x02 & 7 == 7 (digital input). The firmware names the mode itself (INFO: DVI1 1080p@60Hz etc.).
  Picture presenter (src/video/input_picture.hpp, built-in landscape or BMP) follows bank 0x10 reg 0x19 mute + display/capture windows. Runtime: hdmi-signal=MODE|off,
  input-picture=FILE. Live page "HDMI input" box. Write log is now keyed by bank-resolved storageKey.

## Session 13 log (Claude, 2026-11-04)
- HDMI colour path (3f04c63): presenter converts to YCbCr and applies the firmware's VOP matrix (bank 0x10 regs 0x26..0x2E, s2.10 sign-magnitude, Contrast diff-tested)
  + brightness offsets bank 0x0F regs 0x36..0x38 ((v-0x400)/4). 3D (f5f8b25): **3D key = raw 0x00** (only with a signal); SC bank 0x12 regs 0x10..0x13 eye bases (2-px units),
  0x16 pitch, 0x17 fetch, 0x03=0x202 two-eye; output = 50/50 mix (assumed passive). docs/hdmi-input-model.md.
- Scratchpad helpers (recreate if lost): hdmi_try.py (NO SIGNAL snapshot -> SRCKEY raw -> KEYS_AFTER ir-script; native, ~10 s per 400 M), hdmi_sweep.py (per-key sweep), d3_runs.py
  (write-log diffs via --diagnostic-riu-write-log FROM; log in out.txt "diagnostic_riu_write_log", keys = storageKey 0x81000000+(bank<<10)+off). Native runs cannot --save-state.
- NEXT (user request, ATV first): ATV like HDMI with user-chosen frequencies + pictures + interference, then USB JPEG/video, HDMI video/m3u8.
  ATV facts: TV source = raw 0x4B; FRONTEND_TUNER_TYPE UNKNOWN; tuner bus = GPIO I2C (src/peripherals/gpio_i2c.*, no slave, events i2c_start/stop);
  blockers in docs/atv-signal-model.md (status 806B92C0, tracker 80475A18 needs flag 806BEDF0 bit0 + VIF/AFC RAM 806E3FC4/806E3FCC). Plan: log tuner I2C during
  Auto Tuning (MENU->CHANNEL), identify tuner driver/address, add a tuner slave model (PLL freq from divider bytes), VIF lock/AFC from tuned-vs-station offset,
  AVD HTotal/lines from the existing PAL model, picture via the HDMI-style presenter with noise ~ offset.
- ATV step 1 done: `MANTA_I2C_LOG=1` prints GPIO-I2C bytes to stderr. TV source (raw 0x4B) -> 66 transactions on bus 1F203D11, each = address **0xC0** + 4 bytes
  (classic PLL tuner write: DB1 DB2 CB BB). All logged bytes read C0: decode suspect (check bit sampling / NACK path in gpio_i2c.cpp) before decoding the divider. Next: fix decode, ACK as a PLL tuner slave, compute tuned frequency.
- Session 13b: ATV works (55ef6b4, 4b59d1b): PLL tuner 0xC0 (RF = div*62.5k - 38.9M), station model in Riu::get (3501=0x78, status F005,
  AVD block, CR lock/FOE fully KNOWN - zero-unknown zeroes partly known bytes), live page "Antenna (ATV)" box, docs/atv-tuner-model.md.
  Native debug env knobs: MANTA_I2C_LOG, MANTA_READ_HIST, MANTA_RAM_DUMP=ADDR+LEN (summary "ram_dump"), MANTA_LOCK_PC, MANTA_FOE_CONST.
- NEXT (user order, chosen: teletext first): analog has no EPG; channel names come from TTX packet 8/30, so model the VBI slicer:
  firmware teletext core = core/middleware/teletext (TT_DriverIntRam.c, "TTX packet overflow", OS_VBI_MUTEX). Find the VBI slicer
  registers (MDrv_VBI_* / TTX DMA ring buffer in DRAM), feed packets (page 100 + 8/30 name) only while tuned to a station; then names/desc.
  Untested: Auto Tuning storage, station resend after Restart.
