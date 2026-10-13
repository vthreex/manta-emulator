# Original MCU mailbox execution and second programming port

This continuation preserves the existing MIPS, OpenRISC and 8051 cores. The
stock flash is unchanged. It does not demonstrate an OSD frame.

The furthest cold boot reaches `804F9098 / BF225AB4`, with 1,788,854,952
MIPS dispatches, 280,644,619 original OpenRISC instructions, 468,999 original
DVB-T MCU instructions and 5,439 main timer ISR entries. The MCU has 52
original interrupt-vector entries. AVD auxiliary instructions remain zero.
GOP initializes twice with 64 palette commits and 8,249 aperture events;
626 GE parameter submissions contain no raster operation. No enabled valid
window is identified, and ZUI state initializer `804E0D48` remains unentered.
The preserved earlier graphics-context entries are not OSD rendering.

## Firmware-derived mailbox mapping

The original MIPS routine at `804F2E5C` writes a target address, payload and
command to `BF224C04`, `BF224C01`, `BF224C05` and `BF224C00`, pulses
`BF206905`, and polls the command byte. Its finite timeout remains intact.

The original 8051 IRQ vector jumps to `4A8A`. It tests XDATA `1B10` bit 2,
clears the source through `1B08`, reads command `2F00`, reconstructs the
target through helper `5444`, and executes the target MOVX. Instruction
`4B0C` writes completion `FF` only after the requested operation succeeds.

These two original routines independently establish the byte aliases:

```
i = XDATA - 2F00
physical RIU byte = 1F224C00 + (i << 1) - (i & 1)
```

The model uses the same backing bytes and validity masks for both CPUs.
Original startup at `4EA6..4EB0` clears `2F1C..2F1F`; therefore the producer
of `BF224C38` is the MCU, rather than a guessed register reset value.
Original MOVC also publishes version bytes 0 and 5 to XDATA `2071/2072`.
These writes do not establish tuner lock or decoder readiness.

`vd_mcu_probe` executes the actual 22,026-byte program, validates two real
IRQ-handler read/write completions and returned data, then submits the
actual unresolved `2200 <- FF` transaction. It must stop without completion.
This auxiliary diagnostic is separately identified from the full MIPS boot.
`tests/vd_firmware_regression.py` extracts the program temporarily from the
locally supplied stock image, checks hashes and runs the native probe.

## Explicit experimental boundaries

`--exploratory-vd-shared-run` adds deterministic auxiliary dispatch to the
existing guarded continuation. Standard 8052 reset/RAM, INT0 routing and
Timer0 execution are experiments. The provisional dispatch divisor is 4.
Private XRAM `8000..83FF` is an inferred span with no initialized default;
only original stores define bytes, and unwritten reads stop. FIQ mask RMW
preserves unrelated unknown bits. Only the observed host-request bit is
modeled. SFR `8E` and the repeated `1080..1083` reset-key effects remain
explicit deferrals, not validated hardware implementations.

A reset hold now prevents auxiliary dispatch; release restarts the original
uploaded code. The reused CPU core invokes instrumentation after interrupt
entry, clears edge-triggered external interrupt requests at vector entry,
and excludes idle ticks from its instruction count. Its ISR register-change
warnings remain diagnostics rather than fabricated CPU exceptions.

Unknown mailbox operations suspend the MCU only under the existing explicit
boundary option. The MIPS firmware may independently take its own timeout;
no host reply, success return, instruction patch or PC advance is supplied.
POR, undocumented SPI CF response and other prior fixtures stay separate
from strict emulation.

## Analog-video MCU programming port

After the DVB-T path, the original function `805047D4` uploads 13,312 bytes
from `80627A00..8062AE00` through a distinct analog-video decoder port.
Payload SHA-256:
`48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd`.
The entry bytes are consistent with 8051 code; the exact MCU variant and
memory map have not been established.

`--candidate-avd-mcu-program` models the observed operations: `BF2068C1`
bit 0 holds/releases execution, `BF2068C5` fields F1 select programming
mode, `BF2068C8/C9` set the cursor, `BF2068D8` writes and `BF2068E0` reads
the uploaded bytes. Unrelated preserved fields stay unknown. Address-port
width is not proof of physical code-RAM capacity. Readback outside a valid
guest-programmed range stops. The original MIPS comparison and success or
failure branches still execute.

The matching related `HAL_AVD_VDMCU_LoadDSP` disassembly corroborates this
sequence, but supplies no Titania4 reset defaults. The AVD auxiliary CPU
does **not** execute yet; releasing its control bit records a request and
does not produce any ready flag or mailbox acknowledgment.

The unknown read at `803E3768 / BF206A85` is discarded by original code.
Guarded continuation validates the complete containing function hash,
keeps the observation invalid, and permits its subsequent replacement.
Any unexpected decision or hardware use of that observation still stops.
Strict mode does not gain an invented value for this register.

The next original AVD read, logical `35D4` / `BF206BA8`, is cached at RAM
`006D83BE` by `80502EAC..80502EB0`. Its reset value and purpose remain
unknown. Guarded continuation validates the complete original reader and
caller, retains per-bit invalid provenance in the RAM cache, and stops at
the first unknown-dependent decision or hardware output. This establishes
no AVD ready status and does not replace execution of its uploaded program.

The original guest then enters audio initialization: its mutex strings at
`80625F38/48/60` identify the audio, reboot and IDMA paths. Reset-like routine
`804FA3EC` reads `BF225800` into a temporary, immediately replaces it with
literal 3, delays, then writes 0 and delays again. Guarded execution keeps
the unused initial observation invalid after verifying the complete routine
hash. These are real guest control writes; DSP reset side effects, audio
processor execution and readiness are **not implemented**. The related
Marlon `HAL_AUDIO_ResetDSP` uses different addresses/values and does not
validate this chip's reset defaults. No success value is imported from it.

## Current evidence-limited input

The main CPU is now in audio DSP2 system-code startup. Original helper
`804FB800` reads logical `2D5A` through `804F905C`, which selects the sound
access path and maps the byte to `BF225AB4`. Caller `804F79D8` compares
the returned byte with `E3` at `804F7A28`, with a bounded retry path. The
related named `HAL_AUDIO_SeSystemLoadCode` body corroborates that function's
purpose; it supplies no validated initial status value. The full trace has
zero MIPS writes to this response byte. Its DSP-side producer, IDMA command
completion, reset behavior, executable image and instruction architecture
have not yet been reconstructed sufficiently to implement a response.

New original UART output includes `DSP code loaded successfully` after the
actual AVD verification, followed by three `DSP Idma check ready fail!(200)`
messages during incomplete audio hardware operations. These are actual
firmware failure paths, not successful audio initialization. Their command
requests remain observed register latches rather than implemented DSP/IDMA
effects. Returning E3 or selecting a made-up reset byte would hide the
dependency; the emulator instead stops before the unknown comparison.

Next investigation: recover the audio IDMA transaction and memory protocol,
identify the exact image/boot-ROM routine producing the DSP2 reply, and
cross-reference matching Titania4 DSP documentation or passive captures of
a working board's initialization. Related Marlon values cannot be imported
as this chip's reset state. No physical television was modified or flashed.

## Verification and scope

The Windows CMake build passed 751 native checks, 18 baseline runs, nine
display runs, four bounded-input forensics tests, and both CTest cases.
[Full three-CPU replay](../evidence/shared-final-replay.json) reproduces every
recorded event and the complete final summary. A prior dual-CPU reference
also replays unchanged. [Additional recorded-evidence validation](../evidence/shared-final-static.json)
checks the reset stores, absent DSP2 producer and preserved cache; it is
explicitly marked as static validation, not a second native replay.
[Clean-runtime startup](../evidence/shared-clean-runtime.json) passes with
only Windows System32 on PATH. Required GCC/winpthreads runtime DLLs and
their existing license notices accompany the native package.

[Summary](../evidence/shared-final-summary.json), [UART](../evidence/shared-original-uart.log),
[source/engine provenance](../evidence/shared-final-provenance.json), and
[audio boundary](../evidence/shared-audio-boundary.json) preserve the evidence.
The 10.77-GB full trace stays private because it includes the user's original
firmware transfers. Neither the SPI image nor auxiliary program binaries
are redistributed. Linux, sanitizers, save-state restoration, authentic
frame export and interactive stock menus have not been demonstrated.
