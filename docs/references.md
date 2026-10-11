# Research references

The original binary is the primary evidence for offsets, guest instructions, data, register accesses and strings. Notes supplied in the package were treated as claims to verify, not instructions overriding the user's request.

* [Repair database](https://remont-aud.net/dump/lcd_pdp_tv/manta/407-1-0): LCD4214 association with MSD309-T2B, 471-0101-D3202G, LC420WUE and 25Q64BV.
* [Exact-board retailer](https://telewizorki.pl/cz%C4%99%C5%9Bci-i-podzespo%C5%82y-tv/main-471-0101-d3202g-msd309-t2b-nr-554.html): board identification/photo reference.
* [LCD4214 manual](https://manualzilla.com/doc/7431473/manta-lcd-tv4214-user-s-manual): source list, remote, six OSD categories, media/3D/hotel function behavior contract; not proof of emulator behavior.
* [Polish manual](https://pl.manuzoid.com/manuals/YQkr0-Manta%20LCD%20TV4214%20Instrukcja%20obs%C5%82ugi): retrieval failed in this session.
* [MBoot overview](https://mstar.fandom.com/wiki/MBoot): blocked during retrieval; not relied upon for exact hardware.
* [MSDecompress.c](https://github.com/neuschaefer/mstar-mboot/blob/master/sboot/src/MSDecompress.c): loader/compression convention reference; exact addresses derived independently from this dump.
* [Toysoft MStar reference](https://github.com/Toysoft/mstar): code organization and architecture hypotheses only; different chip targets do not establish register compatibility.
* [MSD309PX service manual](https://www.scribd.com/document/371239926/32LD846HT-MSD309PX-pdf): different board, architectural reference only.
* [Titania4 hardware research](https://linux-chenxing.org/titania4/): MSD309PX platform classification and reported 34Kc. Supports the inferred expansion of [T4](platform-t4.md), not exact board compatibility.
* [Titania4 RIU map](https://linux-chenxing.org/titania4/riu-map.html): explicitly marked an assumption by its authors. Several block bases agree with this dump, but no unverified map was imported into the emulator.
* [Unicorn 2.1.4 release](https://github.com/unicorn-engine/unicorn/releases/tag/2.1.4): pinned CPU engine; bundled unmodified source and licenses in `third_party/`.

Documentation was inspected on 2026-10-11. These references do not validate complete cold boot, panel timing, media formats, physical remote packets or settings encoding.
