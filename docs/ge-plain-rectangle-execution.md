# Original GE plain rectangles execute into guest RAM

This is a limited raster implementation, **not a connected Manta OSD frame**.
The original OSD 3 path still needs format C and blended/scaled command `8040`;
those remain rejected. Connected firmware still stops at HTotal before UI
dispatch and has retired zero raster commands.

## Supported command and guards

`--candidate-ge-rect-fill` explicitly enables command **0030** with existing
candidate GE queues. It neither resets queues nor supplies any register value.
The original constructor's request and the independently pinned
[GE register comparison](connected-graphics-and-original-bitblt.md) establish
the primitive, inclusive endpoints, color halves, clipping, pitch and buffer
fields used here. The related driver is a later SoC; the implementation remains
opt-in and does not assert Titania4 silicon defaults.

The supported subset requires:

- All control-register-0 bits known, value exactly 1: enabled plain operation.
- Known control-1 mask `0372`, value zero: no virtual-command/transform mode.
- Known rotation register, value zero.
- Known destination format **F (ARGB8888)** or **8 (RGB565)**.
- Known destination byte address and pitch, within mapped MIU0.
- Known rectangle, clipping and 32-bit start color fields.
- Nonoverflowing, supported coordinates, pixel-aligned pitch and backing range.

Unknown required bits reject. Format C/9, BitBlt, blending, keying, gradients,
MIU1 and unsupported command modes reject. Source format/address registers are
not required by a solid fill; no unknown source is consumed. Completely clipped
rectangles write zero pixels.

## Completion has an actual cause

The decoder captures a validated immutable request when the guest writes GE
register 60. The existing queue keeps it pending through submission and
execution stages. Only execution retirement performs the row writes. No
success, busy-clear or tag completion is created on submission.

The complete clipped destination range is checked before the first write.
Rows use the programmed pitch, preserve padding and surrounding bytes, and
write little-endian ARGB8888 or RGB565 pixels. The existing machine DRAM writer
updates guest memory and clears overwritten unknown-byte provenance. A missing
backing range throws before writes; a failed effect never retires as success.
Existing stage latency remains explicitly experimental.

Summary fields appear only when the option is enabled:
`candidate_ge_rect_fill`, `ge_rectangles_retired`, `ge_raster_pixels_written`.
Each completed request also emits `ge_rectangle_retired`. None of these enables
a GOP window or creates a television scene.

## Original-instruction validation

The isolated `ge_clear_probe` uses explicit RAM descriptors and the original
`80526540` enable write, destination binding `8051FEF8`, clipping `80520274`,
and unchanged constructor fragment `8049E158..8049E188`.

Two 4x3 cases select RGB565 and ARGB8888 in a mapped test buffer, with pitch 24
and surrounding `A5` sentinels. They execute **3,517 original instructions**.
The original clear requests color **FF000000** and command **0030**. The
original asynchronous call returns while the request is pending and the buffer
is unchanged. Advancing virtual queue time retires one rectangle per case and
writes exactly three rows each: **24 diagnostic pixels total**. Every byte of
padding and all surrounding storage are checked unchanged.

These are original clear instructions acting through the peripheral model,
with synthetic software/input descriptors in isolation. They are not an
original connected framebuffer allocation, displayed menu or host-drawn TV UI.
No diagnostic PNG is presented as an OSD frame.

Native tests additionally cover nonblack channel packing, clipping/padding,
unknown fields, unsupported formats/controls, MIU bounds, unavailable backing,
queue timing and failed-effect nonretirement. Earlier rejected format-C clear
and OSD 3 BitBlt tests remain intact. Changed firmware/resources are rejected,
and repeated probe output is deterministic with a System32-only PATH.

The OSD 3 continuation now executes its original enable write before painting;
the 52 attempted writes include that enable plus the earlier 51-write request.
It still stops at `80526598`, return address **805242B0**, on unsupported `8040`.
No empty-queue hypothesis is interpreted as engine enable.

Four additional original `80523BFC` cases execute **10,087** instructions:
equal 7x42 extents issue `0040`; unequal 7x42 -> 7x44 extents without scaling
return original error 9 without a raster request; driver flag `0200` produces
`8040`, and `100200` produces `C040`. Original `80524448..80524538` and
`805242A0..805242AC` corroborate the mode-bit selection. This establishes the
software relationship of command bit 15 to the requested stretch path, not
the hardware's complete sampling/filter/alpha behavior. No BitBlt is retired.

## Connected result

A full conditional cold run with the option enabled still retires **zero**
rectangles and writes **zero** raster pixels. Every previous trace byte,
nonextension summary field, coverage entry, PM-history entry and UART byte
matches the preceding checkpoint. MIPS stays at `80501BFC`, connected AVD at
`0702/11DB.7`. The extension supplies no HTotal, coefficient selector or UI event.
Full fresh-reset replay and regression results are retained in the validation
manifest when the milestone is committed.
