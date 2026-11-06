# Original TSP transport input

`--candidate-tsp-packets` attaches the configured `.ts` station bytes to a
candidate post-FEC TSP input. It requires the existing original TSP CPU, MIU,
TSP register model and `--exploratory-dtv-stations` configuration. It is off by
default. `--exploratory-tsp-packet-cycles N` supplies an explicit packet period
in guest cycles (zero retains the unpaced diagnostic source). A busy one-slot
input drops actual arrivals and advances the transmitter position. Idle-loop
fast-forward stops at arrival boundaries. No calibrated RF/transport clock is
claimed. The private2Mbps fixture uses234624 cycles at the existing312MHz
virtual-clock approximation; this is a test configuration, not measured silicon.

The host performs hardware PID selection, packet-window reads and section DMA.
The original, unmodified TSP instructions retain section-header processing,
continuity, section assembly state, CRC decisions and notification generation.
There is no host PAT/PMT/SDT parser, channel database, channel-name override,
FEC-state override or DTV picture overlay.

## Evidence and supported operations

The original firmware image SHA is
`69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a`.
`tools/tsp_packet_probe.cpp` executes its instructions on the existing OpenRisc
core. A separate ISA call harness supplies registers and descriptors outside
the original image; it is never installed in connected boot.

- Original `FF8/3C4/1004`: select a byte cursor at `210200`, wait/acknowledge
  `210214.8`, then load actual bytes from `210204`. `210208` is the next word.
- Original `138C..13A0`: packet release command `10`, followed by `0`, at
  `210218`. Startup `1E0C..1E14` also sets bit4 with a same-register RMW.
- Original `E38/E78`: use hardware-selected section ID `21021C` to address
  the section descriptor at `211000 + id*80`.
- Original `10A8`, `418`, `460`: DMA source offset `210220`, count `210224`,
  launch `210218`. First/continuing section fragments use commands `89/09`
  at `10C4/1B84`; original `115C` checks the section CRC register.
- The section descriptor layout matches both the original MIPS save loops
  and the related [MStar T3 register header](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mstar/tsp/drv/titania3/mhal_tsp_reg.h).

Candidate `89/09` semantics reset/continue the MPEG CRC accumulator and copy
the actual selected bytes into the guest ring. Completion is published only
after all writes, `BufCur` and CRC are committed. The original `428` helper
checks movement and clears completion itself. Isolated tests cover two
fragments and both linear/wrapping rings; the independent MPEG CRC check value
for `123456789` is `0376E6E7`, and appending its CRC produces residue zero.

Unsupported DMA commands, unknown descriptors, unmapped RAM destinations and
insufficient free ring space stop the model. Their behavior is not guessed.
Out-of-packet bytes stay invalid. Positive filter matching derives from actual
bytes and guest Match/Mask words; negative matching remains in original `5C4`.

New arrivals invalidate the TSP idle-loop proof. Device writes cannot receive
the ordinary storage-only fast-forward proof. An optional `tsp-packets`
snapshot section saves packet bytes, cursors and acknowledgements; input hashes
must match before restore. Ordinary snapshots retain their existing layout.

The original producer's AEON notification at `2130DC` shares physical halves
`1F202DB4/DB8`; its HK counterpart `21032C` shares `1F202AE0/AE4`. Original
MIPS and AEON getters independently corroborate those pairs. Connected evidence
showed native notification `80010000` and the firmware's source47 mask `7FFF`.
Candidate TSP2AEON source47 therefore uses the actual FIRE bit, the local
SW_INT enable and the guest INTC mask. Consumption and acknowledgement remain
the original AEON ISR's responsibility. The paced connected run reaches 2.15B
MIPS dispatches with notification acknowledged to zero and no CP validity stop.
No callback substitutes a section-ready message.

The OpenRisc core now retains partial validity through shifts and rotates:
original `710/714/71C` can extract the known positive-match bit while reserved
status bits remain invalid. Invalid bits still cannot select branches or
addresses. Tests enumerate every low-byte/sign assignment for shift fixtures.

`--exploratory-dvbt-timer-divider N` provides a separate candidate timeout
counter driven by guest23F0/F2/F3 writes and elapsed cycles. It does not become
done because software reads it. Its clock ratio is explicitly uncalibrated.
`--exploratory-vd-xram-poweron-zero` is an additional, unverified initial SRAM
hypothesis for cold diagnostic runs only; it does not override guest writes,
CPU-reset retention, software FEC states or mailbox responses.

## Connected status

The MCU currently reaches an unresolved initial `2073/74/75` value, subsequently
used by arithmetic at `0637`. These bytes have an original software writer and
are not owned by this packet input. Separate zero/all-one startup brackets may
be run with existing `+init` diagnostics; they are not hardware reset evidence.
Connected channel acquisition and decoded picture have not yet been established.
The explicit private-SRAM-zero/TOP-reset-zero diagnostic reaches original FEC
state6, then stops at `3443` on unknown `213D.0`. Its two native readers choose
different `21B0/21B4` programming; its hardware meaning and reset value remain
unresolved. The ordinary run preserves that distinction and still stops `0637`.
