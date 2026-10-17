# Original 1555 alpha helper is not a proven destination threshold

Original OSD 3 painting calls `8051FC34(FF)` before binding its bitmap. The
wrapper enters `80521ECC`, checks the real graphics-context signature and
software nesting, reads the original GE register-11 shadow, ORs the argument
into its high byte, then submits the result through original `80526540`.

Twelve isolated original-instruction cases now cover initial register words
`005A` and `A55A`, each with arguments `00/01/7F/80/FE/FF`. They execute **2,292**
original instructions. Every case returns 1, preserves the low byte, restores
context nesting and issues exactly:

```
GE[11] = previous_GE[11] | ((argument & FF) << 8)
```

This is an OR, not a replacement of the high byte. For example, the original
helper cannot clear a high bit already set in the supplied shadow. The test
preserves this observed firmware behavior rather than repairing it or adding
a fabricated hardware effect. Initial words are explicit ordinary-register
fixtures written by original `GE_WriteReg`, not claimed silicon reset values.
No raster operation or TV input is created by these cases.

## Interpretation and limit

The pinned later Marlon `libapiGFX.a` contains a named
`MDrv_GE_SetAlpha_ARGB1555` with the same register index, high-byte shift and OR
operation. This strongly supports identifying the original helper with that
API family. Its changed context layout is not copied into the Manta emulator.
Reference code is disassembled privately, never imported or executed.

Current [SigmaStar GFX documentation](https://git.sigmastar.com.cn/html/sgs_docs/D3/NLSSV01C12/platform/MI/gfx.html)
describes separate controls for expanding one-bit source alpha to eight bits,
and for reducing an eight-bit result to a destination alpha bit using a
threshold. That distinction is useful when interpreting this helper; it does
not locate a Titania4 threshold register, establish its reset value, or prove
the behavior of original destination format C. No later-chip threshold or
format rule is transferred into strict emulation.

The original paint request writes register 11's high byte to FF, but that alone
does not justify rendering format C with an invented output threshold. The
plain RGB565/ARGB8888 rectangle implementation remains unchanged. Format-C
clear/BitBlt and the connected AVD/HTotal boundaries remain guarded.

## Validation

[Evidence](../evidence/ge-alpha-validation.json) repeats the whole original
clear/allocation/BitBlt probe, including its previously verified 24 isolated
plain-fill pixels, and the twelve new alpha cases. It rejects modified input,
passes with a System32-only PATH and verifies the immutable firmware hash.
The production executable, core tests and every other probe are byte-identical
to `190c65c`, whose 1,499 native checks, complete regression suites, native
startup and 6,592,741-event fresh-reset replay passed. This diagnostic-only
extension does not claim another connected milestone or a new full-machine run.
