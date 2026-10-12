# Optional HEVC pipeline: feasibility investigation

This is a future enhancement. No HEVC decoding, DVB-T2 reception, TS playback or enhanced source mode is implemented. Stock firmware and the original uploaded TSP code remain unchanged.

The recovered TSP code initializes filter-related register state, polls hardware activity and implements MIPS command/response handling. The executed startup demonstrates housekeeping and interprocessor communication, not transport-stream playback or codec decoding. Its original version-response code is distinct from a video decoder. Dedicated demux/filter hardware and separate video/audio decoder interfaces still need emulation. Replacing TSP code alone does not provide an HEVC decoder.

## External HEVC-to-H.264 bridge

A bridge would need an actual DVB-T2 demodulator or supplied TS source, HEVC decode, compatible H.264 encode, audio handling, and TS remultiplexing with consistent program information, timestamps and clock recovery. FFmpeg provides decode/encode APIs and an MPEG-TS muxer with service/PID/timing controls; this establishes host software building blocks, not MSD309 board compatibility. [Decode/encode API](https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html), [MPEG-TS muxer](https://ffmpeg.org/ffmpeg-formats.html#mpegts).

Feasibility on a real Manta board is **not established**. The exact demodulator-to-SoC interface, pad routing, voltage levels, serial/parallel TS clocking, permissible rate and guest tuner expectations require matching-board evidence. The decoder's supported H.264 profiles, levels, bitrate limits, reference-frame constraints and authorization/configuration state also remain unverified. Firmware strings mentioning H.264 are not sufficient proof of those limits. No invasive TS wiring or physical modification is proposed or performed.

An external receiver supplying decoded video through the TV's existing HDMI input is a different architecture: it performs decoding outside the TV. It would not validate an internal TS compatibility bridge or add HEVC capability to the original TSP. Exact HDMI input behavior remains an emulator implementation dependency.

## Host-assisted decoding in the emulator

The plausible attachment point is the **emulated video decoder/receiver and guest-controlled video buffer**, with the original scaler composing that plane and the original GOP retaining OSD ownership. Host-decoded frames must not be painted directly over or instead of the guest interface.

A future implementation needs the original guest codec-selection path, buffer allocation and format, DMA addresses, decoder command/completion protocol, frame queueing, colorspace/range, pitch/tile layout, interrupt delivery and audio/video timebase. A separate optional enhancement could translate supported input metadata or supply a decoded HDMI source, but it must expose that adaptation explicitly. Neither path can currently be described as working stock DTV behavior.

For scale only, linear 1920×1080 YUV 4:2:0 at 60 frames/s carries 186,624,000 bytes/s before extra copies or tiling; 32-bit RGBA carries 497,664,000 bytes/s. These are arithmetic payload rates, not measured board bandwidth or hardware capability claims. Source TS bitrate is a different quantity.

Keep stock hardware modeling, explicitly exploratory hardware fixtures and any future HEVC enhancement separate. No alternative TSP firmware, FFmpeg dependency, fabricated decoder completion or modified TS data was introduced during the boot investigation. Authentic graphics and video-device interfaces must be established before connecting a host decoder.
