# Original OSD 3 painting reaches GE clipping in isolation

This extends original-instruction tests beyond `804A2FF4`. **Connected boot is
unchanged:** MIPS stops at `80501BFC`, AVD at `0702/11DB.7`. No framebuffer,
raster drawing, enabled GWIN or pixels is established by these tests.

## Original execution

The existing fixture executes the original resource-3 worker, 113-window state
setup, root invalidation and dispatcher. Continuing the stopped CPU with its
registers and stack intact reaches these stages with an empty queue:

| Stage | Original address/observation |
| --- | --- |
| Dirty-region painter | `804A2FF4` |
| Child paint message | `804A071C`, message `1E`, window **4** |
| Component selection | `8049E820 -> 8049E7BC -> 8049D8D8` |
| First component | type **4**, index **65**, descriptor `80686AD0` |
| Dynamic drawing | `8049CD90` |
| Surface geometry getter | `8049DA4C` |
| Width callback | original `805E4974`, reads RAM `806F6A5C` |
| Clip wrapper | `803EA14C -> 80520274` |
| GE context clip API | `80522B64 -> 80526540` |
| Next boundary | `80529430`, framebuffer selection/context access |

The component rectangle is **(531,42,7,44)**. It takes 1,557 original instructions
after the old paint boundary to reach the component, another 35 to the geometry
getter. No queued key/menu event is needed. Type 4 routes through `8049D3E0`,
later passing descriptor field +0 (196) to `803EA1A4`. Further bitmap/resource
execution remains separate work; a descriptor is not raster data.

## Explicit fixture scope

The preceding fixture has no constructed surface. `8049DA4C` only fills its
output when RAM `806D4F9C.0` is set, so that continuation stops **before** the
getter rather than consuming leftover stack bytes as valid geometry.

Sixteen separate synthetic RAM fixtures describe a logical surface, origin X
0 or 3, Y 520, extent 1360x248, and widths 530/531/532/537/538/541/1366/1920.
These are arithmetic boundary tests, not claimed TV configurations or actual
surface allocation. Widths below the left edge expose original unsigned
wrapping and are not valid visible surfaces. Original callbacks/arithmetic run.

Backend tests explicitly supply an ordinary-RAM nested GE context, its original
signature and RIU base. No mutex acknowledgment is supplied. They use existing
RIU/GE behavior with the existing opt-in empty-queue reset hypothesis. No new
hardware values are introduced. Only the consumed masks at original `8052624C`
(bit 1) and `8052639C` (bits 11..15) may be read; other bits remain invalid.

Original instructions issue these four halfword writes in order:

| Physical RIU | GE index | Value |
| --- | --- | --- |
| `1F205154` | `55` | left: 531 |
| `1F20515C` | `57` | top: 42 |
| `1F205158` | `56` | right: clipped inclusive endpoint |
| `1F205160` | `58` | bottom: 85 |

The 16 cases execute 5,216 backend instructions and **64 original GE configuration
submissions**. These are clipping registers, not raster commands. Register `60`
does not execute. Context nesting is restored; the next framebuffer-selection
function is observed before execution. Without experimental GE reset, the
original API stops on the first unknown status read and submits zero commands.
No fixture is applied to the production machine. Mutable linked context RAM is
restored and the complete original instruction/resource image checked unchanged.

## Validation and continuation

The [manifest](../evidence/osd3-paint-validation.json) retains repeatable,
hash-checked original execution, 1,457 native checks, 18 baseline runs, nine
display runs, auxiliary/clock/VIF/AVD/provenance suites, four forensics tests,
two CTest entries and clean-PATH Windows startup.

Only the isolated probe/regression changed. The production executable remains
byte-identical to the binary whose complete 6,592,741-event cold execution and
fresh-reset replay passed. This is replay identity verification, **not a new
connected rendering run**. Strict summaries with observation on/off also match.

Next recover actual surface/bitmap context initialization and the original
resource callback before advancing framebuffer selection and bitmap drawing.
Connected HTotal and `11DB.7` still need real producers; clipping does not solve
those inputs. No authentic image or PNG exists yet.
