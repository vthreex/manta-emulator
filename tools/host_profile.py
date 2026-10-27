"""Sampling profiler for the HOST code of the emulator (which C++ functions burn the time), Windows only, no instrumentation.

usage: host_profile.py SECONDS TOP_N -- emulator.exe args...
Starts the emulator, waits 2 s, then every ~1 ms suspends the busiest thread, reads its instruction pointer and resumes it; symbolises the
addresses with nm (the executable must not be stripped) and prints the top functions by sample count.
"""
import collections
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.OpenThread.restype = wt.HANDLE
THREAD_ALL = 0x1F03FF
CONTEXT_CONTROL = 0x100001
TH32CS_SNAPTHREAD = 4


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ThreadID', wt.DWORD), ('th32OwnerProcessID', wt.DWORD), ('tpBasePri', wt.LONG),
                ('tpDeltaPri', wt.LONG), ('dwFlags', wt.DWORD)]


def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    e = THREADENTRY32()
    e.dwSize = ctypes.sizeof(e)
    out = []
    ok = k32.Thread32First(snap, ctypes.byref(e))
    while ok:
        if e.th32OwnerProcessID == pid:
            out.append(e.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(e))
    k32.CloseHandle(snap)
    return out


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD), ('th32ProcessID', wt.DWORD), ('GlblcntUsage', wt.DWORD), ('ProccntUsage', wt.DWORD),
                ('modBaseAddr', ctypes.c_void_p), ('modBaseSize', wt.DWORD), ('hModule', wt.HMODULE), ('szModule', ctypes.c_char * 256), ('szExePath', ctypes.c_char * 260)]


def module_base(pid):
    snap = k32.CreateToolhelp32Snapshot(8 | 16, pid)
    e = MODULEENTRY32()
    e.dwSize = ctypes.sizeof(e)
    base = None
    if k32.Module32First(snap, ctypes.byref(e)):
        base = e.modBaseAddr
    k32.CloseHandle(snap)
    return base


def modules(pid):
    snap = k32.CreateToolhelp32Snapshot(8 | 16, pid)
    e = MODULEENTRY32()
    e.dwSize = ctypes.sizeof(e)
    out = []
    ok = k32.Module32First(snap, ctypes.byref(e))
    while ok:
        out.append((e.modBaseAddr, e.modBaseSize, e.szModule.decode()))
        ok = k32.Module32Next(snap, ctypes.byref(e))
    k32.CloseHandle(snap)
    return out


_exports = {}


def resolve_export(mod, addr):
    import bisect, os
    base, size, name = mod
    if name not in _exports:
        path = os.path.join(os.environ.get('SystemRoot', 'C:/Windows'), 'System32', name)
        out = subprocess.run(['objdump', '-p', path], capture_output=True, text=True).stdout.splitlines()
        syms, on = [], False
        for ln in out:
            if 'Ordinal/Name Pointer' in ln:
                on = True
                continue
            if on:
                parts = ln.split()
                if len(parts) >= 3 and parts[0].startswith('['):
                    try:
                        syms.append((int(parts[-2].replace('0x', ''), 16) if False else int(parts[2], 16), parts[-1]))
                    except ValueError:
                        pass
        syms.sort()
        _exports[name] = ([x[0] for x in syms], syms)
    keys, syms = _exports[name]
    off = addr - base
    i = bisect.bisect_right(keys, off) - 1
    return '[%s] %s+%#x' % (name, syms[i][1], off - syms[i][0]) if i >= 0 else '[%s]' % name


def cpu_time(h):
    c, ex, kern, user = (wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME())
    k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(ex), ctypes.byref(kern), ctypes.byref(user))
    return ((user.dwHighDateTime << 32) | user.dwLowDateTime) + ((kern.dwHighDateTime << 32) | kern.dwLowDateTime)


def main():
    secs, top = float(sys.argv[1]), int(sys.argv[2])
    cmd = sys.argv[sys.argv.index('--') + 1:]
    exe = cmd[0]
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.PIPE)
    time.sleep(2.5)
    handles = {}
    image_base = module_base(p.pid)
    for tid in threads(p.pid):
        h = k32.OpenThread(THREAD_ALL, False, tid)
        if h:
            handles[tid] = h
    before = {t: cpu_time(h) for t, h in handles.items()}
    time.sleep(1.0)
    after = {t: cpu_time(h) for t, h in handles.items()}
    busy = max(handles, key=lambda t: after[t] - before[t])
    print('sampling thread %d (%.0f%% of one core)' % (busy, (after[busy] - before[busy]) / 1e7 * 100))
    h = handles[busy]
    mods_saved = modules(p.pid)
    buf = ctypes.create_string_buffer(1232 + 16)
    addr = (ctypes.addressof(buf) + 15) & ~15
    ctx = (ctypes.c_char * 1232).from_address(addr)
    hist = collections.Counter()
    callers = collections.Counter()
    proc_handle = k32.OpenProcess(0x1F0FFF, False, p.pid)
    image_size = [m for m in mods_saved if m[0] == image_base][0][1]
    t_end = time.time() + secs
    n = 0
    while time.time() < t_end:
        ctypes.memset(addr, 0, 1232)
        ctypes.c_uint32.from_address(addr + 0x30).value = CONTEXT_CONTROL
        if k32.SuspendThread(h) == 0xFFFFFFFF:
            break
        if k32.GetThreadContext(h, ctypes.c_void_p(addr)):
            rip = ctypes.c_uint64.from_address(addr + 0xF8).value
            hist[rip] += 1
            n += 1
            if not (image_base <= rip < image_base + image_size):
                rsp = ctypes.c_uint64.from_address(addr + 0x98).value
                sb = ctypes.create_string_buffer(256)
                got = ctypes.c_size_t(0)
                if k32.ReadProcessMemory(proc_handle, ctypes.c_void_p(rsp), sb, 256, ctypes.byref(got)):
                    for j in range(0, got.value - 7, 8):
                        v = int.from_bytes(sb.raw[j:j + 8], 'little')
                        if image_base <= v < image_base + image_size:
                            callers[v] += 1
                            break
        k32.ResumeThread(h)
        time.sleep(0.0005)
    try:
        p.stdin.write(b'QUIT\n')
        p.stdin.close()
    except Exception:
        pass
    p.kill()
    nm = subprocess.run(['nm', '-n', '--defined-only', '-C', exe], capture_output=True, text=True).stdout.splitlines()
    syms = []
    for line in nm:
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[1] in 'tTwWiI':
            syms.append((int(parts[0], 16), parts[2]))
    syms.sort()
    import bisect
    keys = [s[0] for s in syms]
    base = image_base
    # nm addresses are relative to the preferred image base stored in the PE header
    import struct
    with open(exe, 'rb') as f:
        d = f.read(4096)
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    pref = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
    syms = [(a - pref if a >= pref else a, n) for a, n in syms]
    syms.sort()
    keys = [s[0] for s in syms]
    agg = collections.Counter()
    mods = modules(p.pid) if False else mods_saved
    for a, c in hist.items():
        other = [m for m in mods if m[0] <= a < m[0] + m[1] and m[0] != image_base]
        if other:
            agg[resolve_export(other[0], a)] += c
            continue
        i = bisect.bisect_right(keys, a - base) - 1
        agg[syms[i][1] if i >= 0 else hex(a)] += c
    print('%d samples' % n)
    for name, c in agg.most_common(top):
        print('%5.1f%%  %s' % (100.0 * c / n, name[:150]))
    print('--- who called out of the exe (nearest return address on the stack) ---')
    cagg = collections.Counter()
    for a, c in callers.items():
        i = bisect.bisect_right(keys, a - base) - 1
        cagg[syms[i][1] if i >= 0 else hex(a)] += c
    for name, c in cagg.most_common(top):
        print('%5.1f%%  %s' % (100.0 * c / n, name[:150]))


if __name__ == '__main__':
    main()
