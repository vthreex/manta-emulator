# Front-end protocol (Qt control centre <-> emulator)

`frontend/manta_control.py` starts the emulator (`manta_native_full.exe` or `manta.exe`) with `--frontend-shm NAME`. In that mode the emulator opens no window:

* **Picture**: a named shared-memory block `NAME` (Windows file mapping, 1024-byte header + up to 1920x1080x4 bytes). Header (little endian): `u32 magic 'MNTA'`, `u32 version (1)`,
  `u32 seqBegin`, `u32 seqEnd`, `u32 width`, `u32 height`, `u32 flags` (bit0 guest run finished, bit1 on-screen keyboard/browser page open), `u32 reserved`, `u64 checksum`,
  `char label[256]` at offset 40, `char description[480]` at offset 296. The writer sets `seqBegin = n`, writes the pixels, then `seqEnd = n`; a reader accepts a frame only when both
  are equal before and after copying. Pixels are BGRA words: alpha `0xFF` = a pixel of an enabled GOP window (guest framebuffer, ARGB1555 expanded), alpha `0` = backdrop (the exploratory
  video-plane colour or picture, or black). Nothing is composed by the front end except showing/hiding that backdrop.
* **Input**: text lines on stdin: `KEY FUNCTION` (a remote function name from `configs/input/ir-keymap.tsv`, or an on-screen-keyboard function such as `OSK_CHAR:a`), `RAW 0xNN`
  (a raw IR byte, delivered like `--ir-script raw=0xNN`), `QUIT`. Functions without an entry in the key map are logged by the machine as undeliverable; no code is invented.
* **Pacing**: `--preview-max-fps N` limits composing/publishing by wall time; frames whose GOP memory did not change are not recomposed (`publishPreviewLive`). These are observation-only:
  guest state and the deterministic replay are unaffected.
* **RAM**: `--miu1-mb N` sets the size of the second DRAM bank (default 128 = stock board, multiple of 16, up to 352). MIU0 stays 128 MB. A non-default size is a different machine:
  saved states do not apply.
