# Original AVD co-channel flag publication

Following `360e83a`, the MIPS co-channel getter has an original MCU-side
software producer. Under the candidate shared mapping, native **11CA** maps
to **BF206B94**. Its bit 3 is the field consumed by original `80503080`.

Original AVD startup `0241..0245` uses `CLR A`, `MOV R5,A`, `MOV R7,#CA` and
helper `02AA` to write **11CA=0**. This instruction sequence is after the
initialization routines that currently stop at `0702/11DB.7`; it has **not
executed in connected boot**. The host must not perform this write in advance.

The original recurring publisher is:

```
1130 MOV DPTR,#11CA
1133 MOVX A,@DPTR
1134 MOV 50,A
 ... other individual status bits are updated ...
1183 JNB bit63,118B       ; bit-address 63 = IRAM 2C.3
1186 ORL 50,#08
1189 SJMP 118E
118B ANL 50,#F7
118E MOV R5,50
1190 MOV R7,#CA
1192 LCALL 02AA
```

The detector's private latch is set by `2BB1 SETB bit63` and cleared at
`28D9`, `2BBC` or `2BC1`. Nearby code accumulates private values at `8054/8055`,
applies threshold/hysteresis tests and tests another private control bit.
The complete physical input-to-latch algorithm is not yet established. An
internal latch clear is not proof of a documented disconnected-input value.

`avd_cochannel_probe` executes a separate concatenation of **unchanged original
fragments** and their unchanged absolute helpers. Known fixtures use the
original SETB or CLR instructions; they do not claim that original detection
naturally chose those paths. Each first performs the original startup-zero
publication, then the original shared-byte read and bit-3 publisher. Actual
8051 stores feed the MIPS-visible backing, and the original MIPS getter runs
after each publication. It returns 0 after startup and 0/1 after the real
publication instructions, matching their stored bit.

The regression also runs the existing **complete original AVD image from its
unchanged reset vector** in both previously established, explicitly synthetic
`11DB.7` cases. These isolated prefixes execute 2,278/2,309 instructions,
reach startup `0245`, and perform exactly the expected `11CA=0` store before
stopping at the unknown event decision `0993`. That strengthens the startup
writer evidence beyond the concatenated fragment. Neither selector fixture
enters the TV, and no connected AVD progress is inferred. The new regression
does not add or relax any startup assumption or event acknowledgment.

Two more cases use original MOVX/MOV instructions to copy INVALID fixture
data into the private latch. Default mode stops before storing unknown RAM;
the existing symbolic-data mode preserves it and stops at the original JNB.
Both stop without a second publication. Initial shared status is INVALID;
only the original startup instructions make the fixture output known. The
fixture uses the existing standard-reset CPU experiment and does not infer a
physical IRAM reset value from that experiment.

Four isolated cases execute 157 original 8051 fragment instructions and six
original MIPS getter invocations. They generate no interrupts, external video
signal, command acknowledgment or connected status. The original main image's
instructions/resources remain unchanged; the linked getter context is an
explicit ordinary RAM fixture. Original input files are never modified.

The [regression evidence](../evidence/avd-cochannel-validation.json) records two
identical probe executions, corrupted-input rejection, immutable hashes and
unchanged production binaries. Existing 1,457 native checks and full regression/
6,592,741-event replay remain applicable to the same emulator binary. This
new diagnostic target imports no processor implementation or reference code.

This identifies an original writer and data path for a conditional later
polling dependency. It does **not** resolve native `11DB.7`, hardware events
at `11FC`, raw HTotal acquisition or the current MIPS stop. Connected AVD
remains at 863 instructions and `0702`; startup `0245` and publisher `1192`
have not executed there. No TV flag is preset. Wrapper 11/source phase 0 still
waits behind platform polling; there are no connected windows or OSD pixels.
