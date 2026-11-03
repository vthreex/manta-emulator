#!/usr/bin/env python3
"""Build the native (recompiled-firmware) runner for the browser with Emscripten: OUT/manta.js + OUT/manta.wasm.

Same sources as native/linux/build.py (machine model, peripherals, snapshot loader, recompiled firmware, Unicorn-lite instead of a CPU emulator),
with -DMANTA_WEB: main() hands the machine to webStart() and the page drives it with manta_step() / manta_key() / manta_frame_*.
No threads (GitHub Pages cannot send the COOP/COEP headers that wasm threads need); C++ exceptions are native wasm exceptions.

usage: python native/web/build.py [OUTDIR] [--force]      (needs the Emscripten SDK in work/dl/emsdk)
"""
import concurrent.futures, glob, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, "..", ".."))
work = os.path.abspath(os.path.join(repo, "..", "..", "work"))
emsdk = os.path.join(work, "dl", "emsdk")
env = dict(os.environ, EM_CONFIG=os.path.join(emsdk, ".emscripten"))
emxx = os.path.join(emsdk, "upstream", "emscripten", "em++.exe" if os.name == "nt" else "em++")
pos = [x for x in sys.argv[1:] if not x.startswith("--")]
out = os.path.abspath(pos[0]) if pos else os.path.join(work, "web")
obj = os.path.join(out, "obj"); os.makedirs(obj, exist_ok=True)
S = lambda *p: os.path.join(repo, *p)
sources = [S("src/main.cpp"), S("src/cpu/engine.cpp"), S("src/cpu/openrisc.cpp"), S("src/cpu/vd_mcu.cpp"), S("src/board/machine.cpp"), S("src/peripherals/riu.cpp"),
           S("src/peripherals/gpio_i2c.cpp"), S("src/storage/flash.cpp"), S("src/storage/sha256.cpp"), S("src/snapshot/snapshot.cpp"), S("src/profile/sampler.cpp"),
           S("src/ui/frontend_host.cpp"),
           S("src/cpu/third_party/emu8051/core.c"), S("src/cpu/third_party/emu8051/opcodes.c"), S("src/cpu/third_party/emu8051/disasm.c"),
           S("native/runtime/runtime.cpp"), S("native/runtime/native_runner.cpp"), S("native/runtime/pages.cpp"), S("native/runtime/page_osk.cpp"), S("native/runtime/page_net.cpp"),
           S("native/runtime/page_browser.cpp"), S("native/runtime/http.cpp"), S("native/runtime/html_text.cpp"), S("native/linux/uclite.cpp"), S("native/web/native_window_web.cpp")]
sources += sorted(glob.glob(S("native/gen/*.cpp")))
inc = ["-I", S("src"), "-I", S("native/runtime"), "-I", os.path.join(work, "unicorn-irq", "include")]
common = ["-fwasm-exceptions", "-DMANTA_NATIVE_RUNNER", "-DMANTA_WEB"]
hdr_mtime = max(os.path.getmtime(h) for h in glob.glob(S("src/**/*.hpp"), recursive=True) + glob.glob(S("native/runtime/*.hpp")))

def compile_one(src):
    o = os.path.join(obj, os.path.relpath(src, repo).replace(os.sep, "_").replace("/", "_") + ".o")
    if os.path.exists(o) and os.path.getmtime(o) > max(os.path.getmtime(src), hdr_mtime) and "--force" not in sys.argv: return o, None
    gen = "native" + os.sep + "gen" in src
    # every source, the emu8051 .c files included, is compiled as C++ (as in native/linux/build.py: the 8051 core is called with C++ linkage)
    cmd = [emxx, "-std=c++20", "-x", "c++", "-c", src, "-o", o] + common + inc + (["-O1", "-fno-strict-aliasing", "-w"] if gen else ["-O2", "-w"])
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    return o, (r.stderr if r.returncode else None)

objs, failed = [], False
with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, (os.cpu_count() or 4) - 2)) as ex:
    for o, err in ex.map(compile_one, sources):
        objs.append(o)
        if err: failed = True; print("FAILED", o); print(err[-3000:])
if failed: sys.exit(1)
exports = ["_main", "_manta_step", "_manta_key", "_manta_dispatches", "_manta_frame_update", "_manta_frame_rgba", "_manta_frame_width", "_manta_frame_height", "_malloc", "_free"]
link = [emxx, "-o", os.path.join(out, "manta.js")] + objs + ["-fwasm-exceptions", "-O2",
        "-sMODULARIZE=1", "-sEXPORT_NAME=MantaModule", "-sINVOKE_RUN=0", "-sEXIT_RUNTIME=0", "-sENVIRONMENT=web,worker,node",
        "-sALLOW_MEMORY_GROWTH=1", "-sINITIAL_MEMORY=536870912", "-sMAXIMUM_MEMORY=4294967296", "-sSTACK_SIZE=8388608",
        "-sEXPORTED_FUNCTIONS=" + ",".join(exports), "-sEXPORTED_RUNTIME_METHODS=callMain,FS,HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8",
        "-sFORCE_FILESYSTEM=1"]
r = subprocess.run(link, capture_output=True, text=True, env=env)
if r.returncode: print(r.stderr[-6000:]); sys.exit(1)
w = os.path.join(out, "manta.wasm")
print("linked %s (%.1f MB wasm)" % (os.path.join(out, "manta.js"), os.path.getsize(w) / 1e6))
