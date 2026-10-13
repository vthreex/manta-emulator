# Candidate ISA disassembler

`2100dasm.cpp` and `2100dasm.h` are unmodified MAME files at
commit `a8acf5e8e91f72ebfb241b42c31eb70e441b12ab`, credited to Aaron Giles
and licensed BSD-3-Clause in their headers.

Source: https://github.com/mamedev/mame/tree/a8acf5e8e91f72ebfb241b42c31eb70e441b12ab/src/devices/cpu/adsp2100

The local `emu.h` is a standalone formatting/buffer interface adapter.
It supplies no execution engine, register model or MStar-specific opcode.
This tool compares an explicitly hypothesized architecture with original
guest-uploaded words. A readable decode is not proof of exact binary
compatibility. Reserved/unknown words remain visible as such.
