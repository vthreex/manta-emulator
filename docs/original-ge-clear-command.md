# Original surface-clear raster request

`ge_clear_probe` executes unchanged constructor fragment `8049E158..8049E188`,
then original `803EAC58 -> 805202C8 -> 80522F20`. Width/height, scratch storage
and a nested software context are explicit synthetic register/RAM fixtures.
No GOP allocator returns a fabricated surface; no framebuffer address is supplied.

The original constructor creates its descriptor with origin (0,0), supplied
extent and colour **FF000000**. Original clipping runs first. Existing
conditional queue reset and bit-origin tracking preserve unknown control fields.
Three nonempty cases (1360x248, 4x3, 1x1) issue this sequence:

| GE index | Original value/purpose |
| --- | --- |
| `59` | 0, through original `80526B4C` |
| `01` | RMW clears `50`; unrelated unknown bits preserved |
| `68/69` | left/top 0 |
| `6A/6B` | right/bottom = width/height minus 1 |
| `70/71` | colour words `0000/FF00` |
| `60` | raster request **0030** |

Request caller is `805232EC`; original `80526540` attempts the store at
**80526598 / BF205180**. The model rejects it with
`GE raster command 60h unsupported; no pixels or completion fabricated`.
Eight preceding configuration submissions are accepted; the drawing trigger
does not enter/retire in the queue. Known control mask is **0372**, not FFFF;
tests do not treat other poison bits as actual hardware values.

Two zero-size cases (0x3 and 4x0) return without submissions. The outer wrapper
overwrites the lower API's error result with descriptor byte +28 at `803EACF0`;
observed return is zero. This is not host-generated successful drawing.
All five cases execute **4,352 original instructions**, including clipping.
Regression repeats, rejects damaged code, checks immutable input and passes
System32-only PATH startup.

This establishes an original raster request, **not an implemented renderer,
allocated framebuffer, enabled GWIN or displayed OSD**. Recover destination,
format, composition and ordered memory effects before allowing retirement.

Original destination setup `805251A0..80525220` uses format `34[11:8]`, address
words **26/27**, pitch **33**. Source setup uses `34[3:0]`, **20/21**, pitch **30**,
as recovered in the [bitmap study](osd3-original-bitmap-source.md). Do not
conflate bitmap-source storage with a display allocation.

The [manifest](../evidence/osd3-bitmap-validation.json) includes this regression
and complete existing suites. Production binary and full replay remain unchanged.
