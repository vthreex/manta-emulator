# The MStar firmware in the browser (WebAssembly)

The showcase's **Live firmware** mode runs the original, unmodified MStar LCD4214 firmware in the visitor's browser. Nothing is recreated in JavaScript:
the page runs the same machine as the desktop control centre.

## What runs
- `native/gen/*.cpp`: the firmware, statically recompiled from MIPS to C++ (see the native port docs; differential test 567,901 trials, 0 mismatches).
- The emulator's machine model (`src/`): RIU peripherals, GE/GOP graphics, timers, interrupts, flash, the co-processor models, the snapshot loader.
- `native/linux/uclite.cpp` ("Unicorn-lite"): register and memory store only; no CPU emulator is linked (the firmware executes as compiled code).
- `native/runtime/native_runner.cpp`: the takeover from the save state is shared with the desktop build (`nativeTakeover`); with `-DMANTA_WEB`
  `main()` hands the machine to `webStart()` and the page drives it with `manta_step(n)` (resumes `run_guest` for about n guest instructions),
  `manta_key(function)` (virtual remote: `raw=0xNN` bytes through the emulated PM IR FIFO) and `manta_frame_*` (latest GOP preview frame as RGBA,
  composed exactly like the desktop presenter).

## Build
```
python native/web/build.py            # Emscripten SDK in work/dl/emsdk; output work/web/manta.js + manta.wasm (~38 MB)
python native/web/make_site_config.py work/web work/checkpoints/atv-boot.snap   # fills site/live/
node native/web/node_test.mjs work/web ARGS.json SNAPSHOT FIRMWARE OUTPREFIX     # headless check (speed, MENU frame)
```
Single-threaded on purpose: GitHub Pages cannot send the COOP/COEP headers that wasm threads need. The page runs the module in a Web Worker
(`site/live/worker.js`) in 25 ms slices and posts new frames to the page. C++ exceptions are native wasm exceptions (`-fwasm-exceptions`).

## Configuration
`site/live/config.json` holds the control-centre live profile (`frontend/emulator_session.py`, `build_command(..., 'live')`) with host paths remapped
into the wasm file system. The policy files keep their exact relative paths, because those are part of the save state's configuration fingerprint.
The labelled exploratory options of that profile are in force (e.g. the generated test picture behind the OSD, `--exploratory-menu-no-autoexit`).
The firmware image is copied from `firmware/` by the Pages workflow at deploy; the save state `site/live/state.snap` is committed.

## Measured
Node 24 (Windows, other emulator runs in parallel): 17–43 M guest instructions/s, the same order as the native desktop build. In the browser pane:
about 17 M/s; MENU and the arrows produce the same frames as the desktop build.
