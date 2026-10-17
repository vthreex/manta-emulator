# Original GE destination binding and MIU address encoding

The extended isolated `ge_clear_probe` executes four original format-capability
cases through **80525E28**, and eight destination-descriptor cases through
**8051FEF8 -> 80524F40**. No allocation or pixel memory is supplied; descriptor
addresses deliberately point to unmapped memory. Only register programming runs.

Original capability results:

| Format/use | Return | Base/pitch alignment fields |
| --- | --- | --- |
| 9/source | 1 | 2/2 |
| C/source | 6 (unsupported) | error descriptor 4/4 |
| C/destination | 1 | 2/2 |
| F/destination | 1 | 4/4 |

The pinned later Marlon [shared enum](https://github.com/Toysoft/mstar/blob/5d447aa5b54b60ede27d5fda42d481efa4b10ff8/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/include/MsTypes.h)
calls 9 ARGB1555, C ARGB1555_DST and F ARGB8888. This supports terminology;
the table above is actual original Manta instruction execution. C's rejection
as a source and two-byte destination alignment are verified independently of
that reference. Do not substitute a 32-bit destination for the OSD's raw C.

Destination fixtures use width 1360, height 248, formats 9/C/F/4 and pitches
2720/2720/5440/1360. All return naturally, restore nested context depth and
issue five original configuration writes:

1. `34`: destination format in bits 8..11.
2. `26/27`: packed destination address.
3. `33`: byte pitch.
4. `01`: original control RMW, known mask **0372**, other bits remain invalid.

Each format is tested with physical address `01000000` and `09000000`.
Original **80525DD0 / 80525DD8 / 80525DE8** pack the second address as
**81000000**: physical MIU bit 27 becomes packed bit 31, lower 27 address bits
are retained. Thus register 27 is `0100` or `8100`; register 26 is zero.
These are byte addresses, distinct from GOP's eight-byte address units. This
proves the original software packing scheme, not populated MIU capacity.

Only format **4**, not C, sets context byte +53D for palette handling. The eight
binding cases execute **4,226** original instructions. No raster request, guest
memory write, active surface or scan-out is implied by a successful descriptor
binding. The earlier [clear requests](original-ge-clear-command.md) still stop
at unsupported command 0030.

[Validation](../evidence/ge-destination-validation.json) repeats the complete
clear/capability/binding probe, rejects changed code and tests clean PATH.
Production, core-test and other probe binaries remain identical to the prior
fully validated milestone; no new full-machine replay is claimed or needed
for this isolated-test-only extension.
