# Original OSD 3 bitmap lookup and GE source programming

This continues the [isolated paint investigation](osd3-paint-and-ge-clipping.md).
It does not advance connected cold boot past HTotal or create a framebuffer.

## Resource provenance and original execution

The original `804044EC..80404500` initialization fragment registers bitmap
callback **803EE5C0** through `8051F91C` and font callback **803EE508** through
`8051F900`. No host callback implements either function.

Bitmap resource `0816` resides at flash `002E3C58`, compressed extent 230,144
bytes. The existing read-only directory parser validates raw DEFLATE EOF,
uncompressed-length trailer, physical bounds and immutable input hash. Decoded
length is 2,310,744, SHA256
`67811a4bd3b115e8221a8d233646f4f036be76e0d3cc585353fd92bed675f0aa`.

The diagnostic starts **after decompression**, with these exact bytes at
physical `028AA000`. It executes unchanged original instructions
`803E57C8..803E58C8`, stopping before the enclosing function's epilogue. They
parse the original header, copy **1,816 metadata bytes** to `027AA000`, and copy
the **2,308,928-byte pixel pool** to `03C3D000`. Both copied ranges are compared
with original resource bytes. This is 16,175,389 executed MIPS instructions in
an isolated fixture, not additional TV instructions or an emulated decompressor.

The full original `803E5498` function uses its original resource-directory
lookup and metadata, followed by `803E53E0 -> 803EE638`, to register **350**
bitmaps. Original `803E457C` initializes its pool cursor. Registration executes
410,295 original instructions. The tests verify all handles, addresses, byte
extents, dimensions, pitches, validity fields and raw format **9** through the
original callback, plus invalid-ID rejection without descriptor fabrication.
Allocation accounts for the complete original pixel pool. These are bitmap
source allocations, not display surfaces.

## Asset requested by the first paint component

The previously observed window-4 type-4 component/index 65 uses descriptor
`80686AD0`. Original `8049D3E0` passes descriptor field +0, **196**, into bitmap
frontend `803EA1A4`. The registered callback identifies its source as:

| Field | Original result in the isolated resource fixture |
| --- | --- |
| Bitmap handle | 196 |
| Physical source | `03D6E128` |
| Width / height | 7 / 42 |
| Pitch | 14 bytes |
| Source extent | 588 bytes |
| Raw native source format | 9; no cross-chip enum naming assumed |
| Source SHA256 | `9807355bf74b824e9ecb65c07ece6dca892f3d0ac7a96627f360c52ab0f1541f` |

The pinned later Marlon [shared format definitions](https://github.com/Toysoft/mstar/blob/5d447aa5b54b60ede27d5fda42d481efa4b10ff8/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/include/MsTypes.h)
name 9 as ARGB1555 and C as a destination-specific ARGB1555 format. This is
supporting cross-platform terminology, not a transfer of silicon defaults or
proof of all Manta pixel/composition rules. In particular, C must not be
mistaken for the reference's ARGB8888 format F.

Its paint destination rectangle was 7x44. Source dimensions are not silently
changed to match that destination. The original bitmap frontend `8051FF80`
calls the registered original callback and reaches source-buffer API
`805254EC` with the actual address, format, dimensions and pitch.

## GE source setup and unknown controls

With the same explicit synthetic nested GE context and existing experimental
queue reset as the clipping tests, original source setup issues:

| Physical RIU | Index | Value |
| --- | --- | --- |
| `1F2050D0` | `34` | native source format 9 (other shadow fields zero only in this RAM fixture) |
| `1F205080` | `20` | source low word `E128` |
| `1F205084` | `21` | source high word `03D6` |
| `1F2050C0` | `30` | pitch 14 |

Without symbolic preservation it stops at **805268DC / BF205004**, because the
remaining control bits are unknown. This contrast test remains present.

A second isolated continuation uses the existing `PreservedBits` tracker on
every original instruction. The `805268DC` sample retains each unknown bit's
exact RIU byte/bit identity. The original AND/OR/copy/store path may return
unchanged unknown fields to the same register. It cannot consume them in a
decision, change them, or copy them into a different peripheral register.
An original fifth write to index 1 clears the source-control field. The known
mask becomes **0372**, with all other bits still invalid. No full control word
is made known. Original source setup returns itself, restores nested-call depth
and reaches `8051FFF8` after **508** instructions. No return value is supplied.

No destination framebuffer is bound, no raster trigger is submitted, no guest
render target is modified, no GWIN is enabled and no image is exported. Source
asset bytes copied by firmware are not evidence of an OSD frame.

## Validation

The extended regression independently derives asset 196's offset from original
header dimensions, compares its hash, repeats execution, rejects corrupted main
code, bitmap resource and directory prefix, and preserves strict on/off observer
summary equality. Full validation is recorded in
[the bitmap-source manifest](../evidence/osd3-bitmap-validation.json).

Production CPU/peripheral implementations and the production executable are
unchanged. The verified 6,592,741-event full replay therefore remains applicable
by binary identity; a new connected rendering run is not claimed. MIPS remains
at `80501BFC`, AVD at `0702/11DB.7`. Next investigate actual destination-surface
construction and original raster-command parameters, while retaining those
independent hardware boundaries.
