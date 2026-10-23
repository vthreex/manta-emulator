#!/usr/bin/env python3
"""Cross-build the native (recompiled-firmware) runner as a static Linux x86_64 program with zig.  python native/linux/build.py [OUTDIR]"""
import concurrent.futures, glob, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, "..", ".."))
work = os.path.abspath(os.path.join(repo, "..", "..", "work"))
zig = os.path.join(work, "dl", "zig", "zig-windows-x86_64-0.13.0", "zig.exe")
pos = [x for x in sys.argv[1:] if not x.startswith("--")]
out = os.path.abspath(pos[0]) if pos else os.path.join(work, "linux")
obj = os.path.join(out, "obj"); os.makedirs(obj, exist_ok=True)
S = lambda *p: os.path.join(repo, *p)
sources = [S("src/main.cpp"), S("src/cpu/engine.cpp"), S("src/cpu/openrisc.cpp"), S("src/cpu/vd_mcu.cpp"), S("src/board/machine.cpp"), S("src/peripherals/riu.cpp"),
           S("src/peripherals/gpio_i2c.cpp"), S("src/storage/flash.cpp"), S("src/storage/sha256.cpp"), S("src/snapshot/snapshot.cpp"), S("src/profile/sampler.cpp"),
           S("src/cpu/third_party/emu8051/core.c"), S("src/cpu/third_party/emu8051/opcodes.c"), S("src/cpu/third_party/emu8051/disasm.c"),
           S("native/runtime/runtime.cpp"), S("native/runtime/native_runner.cpp"), S("native/runtime/pages.cpp"), S("native/runtime/page_osk.cpp"), S("native/runtime/page_net.cpp"), S("native/runtime/page_browser.cpp"), S("native/runtime/http.cpp"), S("native/runtime/html_text.cpp"), S("native/linux/uclite.cpp"), S("native/linux/native_window_linux.cpp")]
sources += sorted(glob.glob(S("native/gen/*.cpp")))
inc = ["-I", S("src"), "-I", S("native/runtime"), "-I", os.path.join(work, "unicorn-irq", "include")]
tgt = ["-target", "x86_64-linux-musl"]
hdr_mtime = max(os.path.getmtime(h) for h in glob.glob(S("src/**/*.hpp"), recursive=True) + glob.glob(S("native/runtime/*.hpp")))   # headers are shared by every object: any change rebuilds all
def compile_one(src):
    o = os.path.join(obj, os.path.relpath(src, repo).replace(os.sep, "_").replace("/", "_") + ".o")
    if os.path.exists(o) and os.path.getmtime(o) > max(os.path.getmtime(src), hdr_mtime) and "--force" not in sys.argv: return o, None
    gen = "native" + os.sep + "gen" in src
    flags = ["-O1", "-fno-strict-aliasing", "-w"] if gen else ["-O2", "-w"]
    r = subprocess.run([zig, "c++"] + tgt + ["-std=c++20", "-x", "c++", "-DMANTA_NATIVE_RUNNER", "-c", src, "-o", o] + inc + flags, capture_output=True, text=True)
    return o, (r.stderr if r.returncode else None)
objs, failed = [], False
with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
    for o, err in ex.map(compile_one, sources):
        objs.append(o)
        if err: failed = True; print("FAILED", o); print(err[-3000:])
if failed: sys.exit(1)
exe = os.path.join(out, "manta_native_linux")
r = subprocess.run([zig, "c++"] + tgt + ["-static", "-s", "-Wl,--gc-sections", "-o", exe] + objs, capture_output=True, text=True)
print(r.stderr[-4000:] if r.returncode else "linked " + exe + " (%.1f MB)" % (os.path.getsize(exe) / 1e6))
sys.exit(r.returncode)
