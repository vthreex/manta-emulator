# Original GE copies, scale encoding and the remaining OSD 3 boundary

This continues `d405a59`. **OSD 3's `8040` operation is not implemented and no
connected OSD frame is claimed.** A narrow `0040` copy now executes through the
existing peripheral queues. Original-instruction diagnostics also recover more
of the actual stretched request, including its dither control.

## Implemented memory effects

`--candidate-ge-bitblt` requires `--candidate-ge`. It supports only fully known,
enabled, unscaled, unrotated, unblended, unkeyed **same-format RGB565 or
ARGB8888** requests. It does not establish queue reset, clock, input or GOP
state. Existing strict defaults are unchanged.

The command snapshots these original register fields:

| GE index | Meaning used by the guarded copy |
|---|---|
| `00` | Must be exactly `0001`; other enabled operations reject |
| `01` | Consumed mask `0372` must be known and zero |
| `20/21`, `26/27` | Source/destination byte addresses, MIU0 only |
| `30`, `33` | Source/destination byte pitches |
| `34` | Source/destination format pair, `0808` or `0F0F` |
| `55..58` | Inclusive destination clip bounds |
| `59` | Must be known zero |
| `68..6B` | Inclusive destination rectangle |
| `6C/6D` | Source origin |
| `6E/6F` | Source width/height, equal to destination extents |
| `60` | Exactly `0040` |

Original `80523BFC` and the related GE driver corroborate this unscaled command
and rectangle encoding. No reference implementation was copied into the model.
Clipping advances the source origin by the removed destination pixels. The
copy preserves bytes, including ARGB8888 alpha, without color conversion.
Rows and padding outside the clipped target are untouched.

The entire target range is checked before reading. All required source rows
are obtained through the existing guest-memory reader before the first write;
that reader rejects hardware-origin unknown bytes. A late invalid/short read
therefore leaves the target unchanged and the queue entry unretired. Missing
backing, MIU1, address/coordinate overflow, pitch violations, possible overlap,
format conversion, dithering, keys, blending and stretch remain guarded.
Effects occur at queue retirement, before completion/tag advancement, using
the existing explicitly provisional queue timing. No DMA cycle accuracy or
interprocessor memory arbitration is newly claimed.

## Original-instruction copy tests

Two isolated cases execute original GE enable/control writes, original clip
setup, source and destination binding, and `80523BFC`. The original function
submits `0040` and returns normally. **6,515 original instructions** produce
six copied pixels per format, **12 pixels total**, in synthetic guest RAM.

- Source `01000800`, source pitch 24, source origin `(1,1)`.
- Destination `01000000`, pitch 24, rectangle `(2,1,3,2)`.
- No guest framebuffer allocator or GOP window is involved in these cases.
- All 4,096 bytes of the mapped page are compared, including source data,
  untouched target bytes and row padding.
- An independent Python byte-slice oracle verifies the reported hashes.

| Format | SHA-256 of complete 4,096-byte diagnostic RAM page |
|---|---|
| RGB565 | `b169287b82ec9ea7dd27b02b42add27b219501d11ebe7cc0214fdd540953d15a` |
| ARGB8888 | `a00680af4a4065a6657991d29b61a7be887b2aa7238c9929cc6d6ffc4ac6094c` |

These test-pattern copies are not Manta artwork and are not exported as an OSD
PNG. The prior 24 original diagnostic rectangle-fill pixels remain verified.

## Original scale calculation and encoding

**800 cases / 496,964 original instructions** execute `8052607C`, its fixed-point
divider `805214E8`, and packer `80526BA0`. Dimensions cover 1 through 4,095,
including equality, up/down scaling and the 32x boundary. Both values of the
explicit software-context flag at HAL context `+510` are tested.

For each axis, the ordinary software result is:

```
ratio = floor(source * 4096 / destination), when source < 32*destination
        FFFFFFFF otherwise
phase = trunc_toward_zero((source-destination)*4096/(2*destination)) & 0FFF
phase |= 1000 when source < destination
```

The original routine forces phase bit 12 from the direction; it does not
simply retain an arbitrary signed 13-bit quotient. With the context flag set
and source height below six, it instead sets vertical phase to `1000`.

The packer writes low ratio halves to `64/65`, phases to `5E/5F`, then packs
ratio extension into bit 15 of `5E/5F`. OSD 3's `7x42 -> 7x44` request produces
`64=1000`, `65=0F45`, `5E=0000`, `5F=1FA3`, consistent with these original
instructions. The aggregate software-result hash is
`a904ebd994796325a1591ddc960dbe4e57261c300b00a728513f497037f01b0c`.

This verifies coefficient generation and packing, **not** hardware sampling,
filter taps, edge handling, interpolation precision or output rounding. Neither
`8040` nor `C040` is implemented as a guessed nearest-neighbor copy.

Six additional cases / 176 instructions execute original `8051F9E8` and flag
builder `8051F938`: a nonzero low-byte mode sets driver flag `00100000`; zero
does not. The related named `MApi_GFX_SetNearestMode` uses the same software
field at global `+11`. Together with the original command-selection tests,
this supports identifying the `C040` choice with the nearest-mode request.
It still does not establish its hardware sampling convention or the exact
non-nearest `8040` filter arithmetic.

## Source keys and dithering are part of the real request

Eight original `805224FC` cases establish source-key programming: enabled calls
write the supplied first color's halves to `16/17`, the second to `14/15`,
update mode bit 0 in `1C`, and set bit 6 in control `00`. Disabled calls clear
bit 6 without inventing key data. Unrelated controls are preserved. These tests
execute **3,492 original instructions** and do not implement a key comparator.

Twelve cases of original `8052272C` execute **2,368 instructions**, changing
only control bit 1 for its low-byte enable argument. The pinned related
Marlon library names the matching operation `MDrv_GE_SetDither`. The actual
OSD 3 diagnostic now explicitly verifies entry into `8052272C` on its way to
`8040`; its final control `0047` includes that bit. A dither matrix/rounding
rule is therefore another missing part of exact format-C painting.

## Format C and actual bitmap 196

The pinned [MStar format header](https://raw.githubusercontent.com/neuschaefer/mstar-mboot/962e8b8258378dded694883a9f9acb7058d34631/sboot/src/macaw12/pm/utopia/inc/MsTypes.h)
names `09` as ARGB1555 and `0C` as **ARGB1555_DST**. It explicitly qualifies
the former as RGB555 on Uranus. Original Manta capability/binding tests agree
with `0C` being a two-byte destination-only format. This is strong API-family
identification, not proof that Uranus's alpha behavior applies to Titania4.

Hash-verified bitmap 196 at `03D6E128` contains 283 words `014E` and eleven
words `7C1F`; every source word has bit 15 clear. It is `7x42`, pitch 14.
The original request targets allocated `00DDD800`, pitch 2,720, format C,
with destination/clip `(531,42)..(537,85)`. It enables source keying, blending,
dither and stretching, programs `11=FF01`, `12=0100`, `13=00FF`, and then
submits `8040` at original PC `80526598` (RA `805242B0`).

The [original 1555 helper](original-1555-alpha-register.md) proves the high-byte
OR into register 11. Current [vendor documentation](https://git.sigmastar.com.cn/html/sgs_docs/D3/NLSSV01C12/platform/MI/gfx.html)
distinguishes source-alpha expansion from destination-alpha quantization on a
later chip. Neither source bit-15 behavior, destination alpha threshold nor
Titania4 filter/dither rounding has been established for this command. Treating
every source pixel as opaque, or using its clear high bit as a proved transparent
alpha, would both be unsupported substitutions. `8040` remains rejected and
unretired; no host bitmap replacement or fabricated completion is used.

## Connected boundary

Connected MIPS still stops at `80501BFC`, requiring HTotal from
`BF206B8D/BF206B90`. Connected AVD remains at `0702 / 11DB.7`, 863 instructions.
No measurement, AVD event, PM response, UI event or firmware patch is added.
The original ZUI initializer returns; connected dispatcher `804E0DC4`, OSD 3
window creation, raster effects and enabled GOP scan-out remain unobserved.
See [validation evidence](../evidence/ge-blit-validation.json) for cold-boot,
replay, regression and immutable-input results.
