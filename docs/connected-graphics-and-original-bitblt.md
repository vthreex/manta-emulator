# Connected bitmap provenance and isolated OSD 3 BitBlt

No connected OSD frame is produced. Connected MIPS still stops at `80501BFC`
on invalid HTotal, AVD at `0702/11DB.7`. The original dispatcher has not run in
connected cold boot. No guest framebuffer writes, GWIN enable or PNG is claimed.

## What connected boot actually contains

The bounded, read-only ZUI observer now records the original bitmap-196 table
slot at `006BB54C`, surface flags and the original static GE context. It never
follows arbitrary pointers. The fixed 588-byte source is hashed only when every
byte is known; one unknown byte suppresses the hash. Raw context fields do not
imply an active destination or scan-out.

At the connected HTotal boundary:

| Field | Original guest state |
| --- | --- |
| Bitmap 196 | valid, format 9, 7x42, pitch 14, 588 bytes |
| Source physical address | `03D6E128` |
| Source SHA-256 | `9807355bf74b824e9ecb65c07ece6dca892f3d0ac7a96627f360c52ab0f1541f` |
| Surface flags | 0 |
| GE context pointer / signature | `806FEB60` / `55AABEEF` |
| GE format shadow | `0909` |
| GE destination low / high / pitch | 0 / 0 / 0 |

This proves that the first isolated paint component's original source artwork
is already loaded and registered in the connected machine. It is not a rendered
framebuffer. The new cold run has the same 6,592,741 trace events and exact trace
SHA-256 as the prior run, with identical CPU/peripheral summary fields, coverage,
PM history and UART outside these added observations. Fresh-reset replay is
recorded in the accompanying validation evidence.

## Original CPU-side GOP allocation

`ge_clear_probe` executes the original initialization fragment
`80533424..805334A0` with explicit ordinary-RAM context/input fixtures. Inputs
`00DDD800` and extent `0072F400` match the earlier guest initialization arguments;
this isolated execution does not assert connected allocation. The fragment
creates its own pool mode, base, length and empty-list sentinel.

Original `8052C3C0` then creates three records across two separate fixtures:
two 1360x248 format-C records in the observed-size pool, and one 8x8 record in an
explicit 128-byte boundary pool. It computes byte pitches, allocation sizes,
addresses and linked-list entries. Six duplicate/out-of-range requests return
255 without changing any GOP metadata. Pixel RAM is deliberately unmapped.

Original callback registration `805293F8` installs `803EE738`. Original
`80529430` reads the guest-created record, invokes that callback and programs GE
destination address, format and pitch. No descriptor or successful return is
substituted. The mutex ownership is an explicit software fixture; the original
`805CB82C` stub itself returns -1. Both fixtures then run original clipping and
constructor-clear instructions to command **0030**, which remains rejected by
the raster guard. These allocation/binding/clear tests execute **14,406** original
instructions. No completion or raster write is fabricated.

## OSD 3 now reaches its original bitmap raster request in isolation

The ZUI probe also executes message 14 creation callbacks for **all 113 original
windows**: 31,569 original instructions. The resulting dirty region still needs
no queued event. Original dispatch again selects window 4, component type 4,
resource 65 at `80686AD0`, whose bitmap index is 196.

A separate explicit surface-geometry/context fixture joins the original GOP
allocator to this painter. It does not invoke UI functions in the production
machine. The stopped original dispatcher continues with its real stack and
registers through:

`804E0DC4 -> 8049F180 -> 804A2FF4 -> 804A071C -> 8049CD90`

then through original clipping, `80529430 -> 803EE738`, bitmap callback
`803EE5C0`, and `803EA1A4`. The source pool and all 350 metadata registrations
come from the hash-verified original resource copies, not generated artwork.

After **5,598** backend instructions and **50** parameter submissions, original
`80526598` attempts command **8040** at `BF205180`. The same existing raster
guard rejects it. The 51st recorded attempted write is not queued or retired.
The diagnostic retains the complete ordered register request and known masks.

Important fields in that original request:

| GE indices | Values |
| --- | --- |
| 55/57/56/58, clip | 531 / 42 / 537 / 85 |
| 34, destination/source formats | `0C09` |
| 26/27/33, destination | `D800` / `00DD` / 2720 |
| 20/21/30, source | `E128` / `03D6` / 14 |
| 68/69/6A/6B, destination endpoints | 531 / 42 / 537 / 85 |
| 6C/6D, source origin | 0 / 0 |
| 6E/6F, source extent | 7 / 42 |
| 64/65, scale parameters | `1000` / `0F45` |
| 60, attempted primitive | `8040` |

The source is 7x42 while the destination rectangle is 7x44. Do not implement this
as an unscaled memory copy or infer the complete meaning of command bit 15 from
its mere presence. The known control mask at register 01 remains **0372**;
unrelated bits stay invalid and may only be preserved to their original lanes.
The synthetic GE software context also does not establish a physically enabled
engine. Rendering still requires correct enable, blending, scaling, format-C
destination behavior, memory effects and command completion semantics.

## Independent register comparison

The GPL [Linux Chenxing GE driver](https://github.com/linux-chenxing/linux/blob/5b16308ae546df24c218e3785942e741f04ae326/drivers/gpu/drm/mstar/mstar_ge.c)
targets `sstar,ssd20xd-ge`, a later device. Its offsets for command, source,
destination, clip, endpoints, pitch and color match these original requests;
its primitive field at command bits 4..6 calls 3 rectangle fill and 4 BitBlt.
It supports the command-family interpretation, **not** Titania4 reset values,
format-C details or transfer of all later-chip control semantics. No driver
code is imported. The pinned source hash and comparison are retained as research
evidence.

## Remaining work

Connected boot still needs the physical AVD coefficient/event/HTotal producers
before platform polling can return naturally. Independent graphics work can
now start from actual original allocation plus clear/BitBlt requests, with a
specific source, destination, geometry and format. Implementing those raster
effects must preserve every unresolved mode and provenance boundary; clearing
the guard alone would fabricate success. No authentic displayed pixels exist.
