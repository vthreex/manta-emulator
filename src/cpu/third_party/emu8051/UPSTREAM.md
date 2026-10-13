The core.c, opcodes.c, disasm.c and emu8051.h come from
https://github.com/jarikomppa/emu8051 at commit
5dc681275151c4a5d7b85ec9ff4ceb1b25abd5a8. See LICENSE and provenance.json.

Only the CPU core is used. No curses frontend is included. The original files
are compiled as C++ for integration and exception unwinding through callbacks.
The Manta wrapper rejects unmapped XDATA and nonstandard SFR operations.
No claim of exact MStar peripheral or clock-cycle/pin compatibility is made.
Existing MIPS and OpenRISC engines are unchanged.

Local opcode corrections: stack push/pop use indirect internal RAM (including
the upper 8052 RAM), rather than direct SFR addressing; auxiliary carry/borrow
uses the low four bits and bit-four boundary. Native tests exercise upper-RAM
LCALL/RET and arithmetic flags. provenance.json records the upstream hashes;
the Git commit records the local patch. Exact MStar cycle timing is unverified.

Further local core fixes invoke the optional instruction-boundary callback
after architectural interrupt entry, clear external edge-mode request flags
at vector entry, and exclude idle/power-down ticks from executed-instruction
counts. The wrapper reports upstream ISR register-preservation warnings as
debug diagnostics; they are not architectural 8051 exceptions. INT0 and
Timer0 are enabled only in the explicitly selected shared-bus experiment.
