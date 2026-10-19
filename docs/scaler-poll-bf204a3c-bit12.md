# BF204A3C bit 12 - two-outcome comparison (session 8)

Reader: `805FAFD8` (`lhu v1,(0xBF200000 + 0x4A3C); andi v1,0x1000`), inside the 0x20-retry poll loop `805FAFCC..805FB014` (sleep `805CBBD8` between tries) of the routine
that starts at `805FA8xx`. Register address `0x1F204A3C` = RIU offset `0x4A3C` (halfword at index `0x25*0x200 + 0x3C`). Its hardware owner and writers are **not identified**;
`--exploratory-mmio-bits 1f204a3d:10:V` supplies only that one bit. Not labelled READY: the meaning is unknown.

Identical connected runs (`b12`, same flags as the NO SIGNAL run s13, budget 1.6B dispatches, no key):

| bit 12 | result | observation |
|---|---|---|
| **1** | runs to the 1,600,000,000 budget, no stop | the poll at `805FAFD8` ran 33 times, each time taking `805FB018` (the branch for bit set), one pass per ~120,000 dispatches; 384 gradient fills, 108 I8 fills; 28 `804C3684` calls |
| **0** | stops at **1,305,989,717** (`805FB000`, `BF204C14` unmodeled read) | the first poll falls through the retry loop head to the second poll `805FAFFC..805FB008` (`lhu BF200000+0x4C14; andi 0xC0`), which has no hypothesis: the run needs a second bit hypothesis to continue |

The outcomes therefore diverge at the first read; they were never joined. Which one matches a TV without a signal is not established, and the second register (`BF204C14` bits 7:6) is another
unmodeled status bit. The ready=1 run is the one that continues the original UI (NO SIGNAL dialog moving, digit banner, etc.). Results: `evidence/scaler-bf204a3c-bit12-comparison.json`.
