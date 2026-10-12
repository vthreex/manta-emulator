# Firmware and resource map

Original: `0x800010` bytes, SHA-256 `d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9`. First 8 MiB SHA-256: `471e1f1c284e3c0fc9af68be0d8a79d0d635e0a1cca31066cd1e6df91e406d83`. The trailer is `bc000000000000000000000000000000`, preserved in private extraction output and never mapped as flash.

The packaged flash copy matched the original's first 8 MiB exactly. The four supplied payloads reproduced their lengths and hashes. A bounded raw-DEFLATE scan of the remaining populated range recovered eight additional streams. Every listed stream has a real end marker, no unconsumed compressed data, and a matching four-byte little-endian uncompressed-length trailer. Additional streams are not called executable/font/media files merely because of entropy or a short signature.

| Compressed start..end (end exclusive) | Expanded bytes | Classification |
|---|---:|---|
| `023880..16124E` | 3,167,072 | Main code/data; load address `803B3980`; guest inflation demonstrated |
| `161258..1BD538` | 731,981 | Unclassified payload |
| `1BD548..1E65B7` | 357,005 | Unclassified payload |
| `1E65C0..1E782B` | 8,448 | Unclassified payload |
| `1EBC40..23C0BD` | 946,752 | Unclassified payload, includes strings |
| `23C0C8..242902` | 82,240 | MCS-51-looking prefix; ISA/function assignment not demonstrated |
| `242910..2B92B9` | 1,253,416 | Similar MCS-51-looking prefix; contains strings; provenance unresolved |
| `2B92C8..2D89C9` | 277,022 | Unclassified payload |
| `2E3C58..31BF4B` | 2,310,744 | Structurally consistent MST type-1 resource; graphics/font codec unresolved |
| `31BF58..342172` | 467,106 | MST type-2 indexed text resource, decoded and bounds-checked |
| `342180..350C7C` | 130,608 | Unclassified payload |
| `350C88..360D0E` | 145,888 | MSVC-prefixed payload; function unresolved |

All full hashes are in [manifest.json](../evidence/forensics/manifest.json). Entropy, original offsets, raw disassembly, direct MMIO references, and call graphs are in [forensic evidence](../evidence/forensics/flash-map.json).

Raw SPI `1E7838..1EBC40` (17,416 bytes, end exclusive) is now identified as the original TSP auxiliary firmware: actual MIPS CPU_SetBase guest arguments, matching complete RAM/SPI SHA-256 `69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a`, and a little-endian OpenRISC register-clear startup sequence. The original guest downloads its first 12,288 bytes into QMEM. This image is not another DEFLATE stream, not 8051 firmware, and not reference code injected into the guest. The focused auxiliary core now executes original startup/handler instructions and its genuine reply; exact silicon reset mapping remains experimental. [Current execution evidence](openrisc-continuation.md); [original download milestone](tsp-continuation.md).

MST type-2 fields are big-endian. Data starts at `1F040`, size `53062`, and exactly ends at the payload length. There are 26 language index tables, spaced `1300` bytes from `240` through `1DD40`. Each begins with count 1,214 and indexes UTF-16LE data by even offsets. All indexes and string termination bounds were checked. Some languages contain control tokens such as `0008`; their rendering/reference semantics remain unresolved. Language-index ordering is not inferred to match a host language selector.

English table IDs 1..6 contain PICTURE/CHANNEL/SOUND/TIME/OPTION/LOCK. Table index 6 contains OBRAZ/KANAŁ/DŹWIĘK/CZAS/USTAWIENIA/BLOKADA. PHOTO/MUSIC/MOVIE/TEXT occur at English IDs 677..680; Hotel Menu at 1181; 3D settings at 1196..1202. This is original indexed resource data, not proof that those menus have been rendered or navigated. Shared HDMI3/HDMI4 strings do not prove this TV has those ports.

`7E0000..7EFFFF` and `7F0000..7FFFFF` differ at local `FA4A` (`1F` vs `1E`). Redundancy, checksum and generation interpretation is unconfirmed. No persistent sector was edited. The erased area is characterized by block entropy/FF counts rather than treated as an exact partition boundary.

Capstone flat MIPS32 LE listings and address-based symbol annotations were used as the available disassembly equivalent. A Ghidra label-import script is supplied; no interactive Ghidra analysis session was claimed. Main scheduler/RTOS, exception dispatch, OSD render path, palette/glyph codec, and all load destinations for the additional streams remain to be recovered.
