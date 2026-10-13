# Manta revision, DVB-T MCU and CI continuation

This continues the **existing** emulator from `ddb1081`. It has **no original OSD frame, enabled framebuffer, or GE raster draw** yet. The furthest original execution below uses explicitly exploratory input and reset assumptions; it does not establish the real board's reset values.

| Observation | Original guest result |
|---|---|
| MIPS | 950,695,929 dispatches; stopped at `804F496C`, byte read `BF224C38` |
| OpenRISC TSP | 71,104,864 genuine instructions; original startup reply still accepted by MIPS |
| DVB-T 8051 | Two original MIPS hold/release requests restart the same uploaded program; 399 completed original instructions after each request (798 across both). Each stops at `55B9`, XDATA `2F00` write of zero. |
| Timer ISR | 2,755 original vector entries |
| New original UART | `DVB-T Load DSP Code OK` after program readback; this is the MIPS loader result, not MCU readiness |
| DVB-T MCU mailbox | 48 original interrupt-request pulses, with no invented `FF` completion or MCU reply; the original MIPS timeout path executes |
| CI card detect | Five original reads of `BF206891` under an explicit bit-2-high external-line fixture; polarity on this PCB remains unverified |
| GOP / GE | GOP initializer `80539CE4` twice; 8,249 aperture events; six original window-programming entries; 330 write32 helper entries; 626 GE parameter submissions; no decoded enabled window or raster effect |
| ZUI / graphics | `8040440C` startup and `80522D5C` graphics context entered once. `804E0D48` ZUI state init, `80536274` CreateWinImpl2, and `80520A84` font callback have zero observed entries. |

## CI and the new MIPS stop

The optional CI fixture supplies a high external level **only** for bit 2. The related [MStar PCMCIA archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libdrvPCMCIA.a) has the same `0x6880 + (index << 1) - (index & 1)` byte-addressing formula as the original Manta reader `8051EF78`; its card-detection routine tests logical index 9 with mask `04`. This corroborates the PCMCIA/CI interpretation, not the Manta connector polarity. All other bits and interrupt status remain unknown.

The earlier `BF2068A0` read is copied through the original wrapper and then overwritten with immediate `51` by its caller. SHA checks on reader, wrapper and caller restrict guarded continuation to that path. Unknown bits copied to RAM retain provenance until overwritten. Native tests reject a branch on those bits.

At `804F496C`, the original function `804F4904` tests bit 0 of `BF224C38` before another command. It contains a finite 201-iteration wait and later acknowledgement tests. No observed original write, MCU instruction, or board-specific reset source establishes bit 0. Strict mode leaves it unknown; the exploratory run also stops before an unknown value changes a branch. This is a second mailbox/status path, separate from the already observed `BF224C00` command timeout. A read-only board register trace, or a matching Titania4 mailbox and 8051 XDATA specification, is needed to model its producer and initial state.

## Revision and original DVB-T auxiliary code

Original `806006CC..DC` reads `BF003C05` and returns `byte < 2`. Its only direct caller `805F4B10` adds two to a capped 16-bit fetch parameter on that branch. A related [MStar SYS archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libdrvSYS.a) reads the high byte at `3C04` into `sysInfo`, whose `GetChipRev` accessor returns the cached byte. This is the best-supported identity. The Manta stepping remains unknown. `--exploratory-fetch-revision-byte 2` is a selected hypothesis; native tests execute both original branches. Strict mode assigns no value.

The MIPS loader `804F565C` programs `BF206918` with original runtime bytes at `806A4A04..806AA00E` and reads **all 22,026 bytes** back through `BF206920`. SHA-256: `2bdb82e863899ea863f63bb4596235615c8f5aa5f77bdc465bec2d465bde22a5`. One shared addressed code store powers both port readback and CPU fetches. No replacement image is installed. The UART identifies the selected payload as **DVB-T**; related AVD loader code corroborates only a similar transfer sequence.

The uploaded bytes decode as an MCS-51 program: reset `LJMP 4DF3`, RAM clear, stack `A5`, startup and interrupt setup. Jari Komppa's [MIT-licensed emu8051](https://github.com/jarikomppa/emu8051) at `5dc681275151c4a5d7b85ec9ff4ceb1b25abd5a8` is vendored with its license and two tested fixes for upper-RAM stack access and the arithmetic auxiliary-carry boundary. The bounded standard-8052 reset/RAM experiment does not establish MStar clock, memory, pin or interrupt wiring.

Without deferral, MCU `55F9` stops on undocumented SFR `8E = 02` after 374 completed instructions. `--defer-vd-sfr8e-write` records that write while keeping its effects unknown; MCU `5577` then stops on XDATA `1080 = AA` after 383. Separately, `--defer-vd-reset-key-writes` admits **only** the original `1080:AA, 1081:55, 1082:AA, 1083:55` writes. The next original instruction attempts XDATA `2F00 = 00` at `55B9` after 399 completed instructions. A changed-key native test stops. Neither deferral produces XDATA read values, a ready flag, interrupt, or MIPS reply. Matching Titania4 evidence is needed for SFR `8E`, the four writes, and `2F00` mapping.

`--exploratory-vd-suspended-boundary` leaves each failed auxiliary transaction pending and lets the original MIPS control store retire. The second hold/release is at `804F48D8` and re-enters the original MCU reset path; it does not upload a second firmware image. The MIPS CPU issues 48 real mailbox pulses and follows its original timeout paths. The MIPS `DVB-T Load DSP Code OK` banner follows byte comparison and does **not** mean the MCU completed startup. This suspended-device fault and the CI fixture are separate from strict hardware behavior.

The original firmware remains locally supplied and immutable. The distributed trace is redacted at both MCU program ports and cannot be used for byte-for-byte replay; full private traces and their hashes remain local. The [summary](../evidence/revision-final-summary.json), [provenance](../evidence/revision-final-provenance.json), and [dual-CPU regression](../evidence/revision-final-dual.json) record the final build and validation. Replaying locally generated raw traces checks deterministic execution up to the explicit stop. Strict POR_STATUS `BF200A38` and the undocumented Winbond `CF` response remain unresolved. No authentic frame, channel scan, USB media, audio, 3D, or PVR function is claimed.
