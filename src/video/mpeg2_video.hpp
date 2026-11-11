#pragma once
// EXPLORATORY model of the MStar MPEG video decoder (MVD) output: the transport packets of the PID the original firmware routes to
// the video decoder are reassembled into PES, the MPEG-2 elementary stream is decoded with libmpeg2 (third_party/libmpeg2), and the
// newest displayed picture is kept for the presenter. The decoder's own RISC firmware is not emulated; pictures are shown at the
// stream's frame rate on the emulator's virtual clock (advance), not at its presentation time stamps.
#include "video/input_picture.hpp"
#include <cstdint>
#include <array>
#include <deque>
#include <span>
#include <vector>

namespace manta {
class Mpeg2Video {
public:
  Mpeg2Video();
  ~Mpeg2Video();
  Mpeg2Video(const Mpeg2Video&) = delete;
  Mpeg2Video& operator=(const Mpeg2Video&) = delete;
  void reset();                                   // new stream (retune, different PID)
  void feedPacket(std::span<const uint8_t> ts);   // queue one 188-byte transport packet of the video PID (decoded on demand)
  // Decode queued data until the picture due at virtual time `cycles` (312 MHz timebase) is shown: pictures follow the stream's
  // frame rate (sequence header), not the packet arrival rate. A queue overflow drops the oldest packets (the decoder resyncs).
  void advance(uint64_t cycles);
  uint64_t droppedPackets() const { return dropped_; }
  size_t queued() const { return queue_.size(); }
  bool hasPicture() const { return serial_ != 0; }
  uint64_t serial() const { return serial_; }      // increments with every decoded displayed picture
  uint64_t pictures() const { return serial_; }
  uint64_t errors() const { return errors_; }
  unsigned width() const { return width_; }
  unsigned height() const { return height_; }
  InputPicture picture() const;                   // newest picture converted to BGRA (BT.601, limited range)
private:
  void decodePacket(std::span<const uint8_t> ts);
  void feedEs(const uint8_t* p, size_t n);
  std::deque<std::array<uint8_t, 188>> queue_;
  uint64_t dropped_ = 0, startCycles_ = 0, decodedSerial_ = 0;
  bool started_ = false, accepting_ = true;
  unsigned framePeriod_ = 1080000;   // 27 MHz units per picture (25 fps until a sequence header says otherwise)
  void takeDisplay();
  struct Decoder;
  Decoder* dec_ = nullptr;
  bool inPes_ = false;
  unsigned pesHeaderLeft_ = 0;
  std::vector<uint8_t> pesHeader_;
  uint64_t serial_ = 0, errors_ = 0;
  unsigned width_ = 0, height_ = 0, chromaWidth_ = 0, chromaHeight_ = 0, lumaStride_ = 0;
  std::vector<uint8_t> y_, u_, v_;
};
}
