#include "video/mpeg2_video.hpp"
#include <algorithm>
#include <cstring>
extern "C" {
#include "../../third_party/libmpeg2/mpeg2.h"
}

namespace manta {
struct Mpeg2Video::Decoder { mpeg2dec_t* d = nullptr; };

Mpeg2Video::Mpeg2Video() : dec_(new Decoder) { dec_->d = mpeg2_init(); }
Mpeg2Video::~Mpeg2Video() { if (dec_ && dec_->d) mpeg2_close(dec_->d); delete dec_; }

void Mpeg2Video::reset() {
  if (dec_->d) mpeg2_close(dec_->d);
  dec_->d = mpeg2_init();
  inPes_ = false; pesHeaderLeft_ = 0; pesHeader_.clear();
  queue_.clear(); started_ = false; accepting_ = true; decodedSerial_ = 0;
}

void Mpeg2Video::feedPacket(std::span<const uint8_t> ts) {
  if (ts.size() != 188 || ts[0] != 0x47) return;
  // Input faster than the frame rate (a short clip looping on the aerial): when the buffer is full, refuse input and resume only at a
  // packet that starts a sequence header, so the decoder only ever sees whole groups of pictures (no broken references).
  constexpr size_t kMaxQueued = 2048;   // ~385 KB of transport packets: about 3 s at 1 Mbit/s, keeps the picture close to the input
  if (!accepting_) {
    bool seq = false;
    if (ts[1] & 0x40) for (size_t i = 4; i + 3 < 188 && !seq; ++i) seq = ts[i] == 0 && ts[i + 1] == 0 && ts[i + 2] == 1 && ts[i + 3] == 0xb3;
    if (!seq || queue_.size() > kMaxQueued / 2) { ++dropped_; return; }
    accepting_ = true;
  }
  if (queue_.size() >= kMaxQueued) { accepting_ = false; ++dropped_; return; }
  std::array<uint8_t, 188> p; std::copy(ts.begin(), ts.end(), p.begin()); queue_.push_back(p);
}

void Mpeg2Video::advance(uint64_t cycles) {
  if (!started_) { if (queue_.empty()) return; started_ = true; startCycles_ = cycles; }
  // pictures due: elapsed seconds * 27 MHz / frame period, the first one at once
  const uint64_t elapsed = cycles - startCycles_;
  const uint64_t due = 1 + (elapsed / 312000000) * 27000000 / framePeriod_ + (elapsed % 312000000) * 27000000 / 312000000 / framePeriod_;
  while (decodedSerial_ < due && !queue_.empty()) {
    const auto p = queue_.front(); queue_.pop_front();
    decodePacket(p);
  }
}

void Mpeg2Video::decodePacket(std::span<const uint8_t> ts) {
  if (!dec_->d) return;
  const bool start = ts[1] & 0x40;
  const unsigned afc = (ts[3] >> 4) & 3;
  size_t at = 4;
  if (afc == 0 || afc == 2) return;               // no payload
  if (afc == 3) { at += 1 + ts[4]; if (at >= 188) return; }
  const uint8_t* p = ts.data() + at;
  size_t n = 188 - at;
  if (start) {
    // PES header: 00 00 01 stream_id, length(2), flags(2), header_data_length(1), header data
    inPes_ = false; pesHeader_.clear(); pesHeaderLeft_ = 9;
  }
  if (pesHeaderLeft_) {
    while (n && pesHeaderLeft_) { pesHeader_.push_back(*p++); --n; --pesHeaderLeft_;
      if (pesHeader_.size() == 9) {
        if (pesHeader_[0] || pesHeader_[1] || pesHeader_[2] != 1 || (pesHeader_[3] & 0xf0) != 0xe0) { pesHeaderLeft_ = 0; ++errors_; return; }   // not a video PES
        pesHeaderLeft_ = pesHeader_[8];
        if (!pesHeaderLeft_) inPes_ = true;
      } else if (pesHeader_.size() > 9 && !pesHeaderLeft_) inPes_ = true;
    }
  }
  if (inPes_ && n) feedEs(p, n);
}

void Mpeg2Video::feedEs(const uint8_t* p, size_t n) {
  // libmpeg2 needs the bytes to stay valid until it asks for more (STATE_BUFFER); the loop below runs to that point.
  mpeg2_buffer(dec_->d, const_cast<uint8_t*>(p), const_cast<uint8_t*>(p) + n);
  for (int guard = 0; guard < 100000; ++guard) {
    const mpeg2_state_t state = mpeg2_parse(dec_->d);
    if (state == STATE_BUFFER) return;
    if (state == STATE_INVALID || state == STATE_INVALID_END) ++errors_;
    if (state == STATE_SLICE || state == STATE_END || state == STATE_INVALID_END) takeDisplay();
  }
}

void Mpeg2Video::takeDisplay() {
  const mpeg2_info_t* info = mpeg2_info(dec_->d);
  if (!info || !info->sequence || !info->display_fbuf) return;
  const mpeg2_sequence_t* s = info->sequence;
  const unsigned stride = s->width, cstride = s->chroma_width;
  width_ = std::min(s->picture_width, s->width); height_ = std::min(s->picture_height, s->height);
  chromaWidth_ = cstride; chromaHeight_ = s->chroma_height;
  y_.resize(size_t(stride) * s->height); u_.resize(size_t(cstride) * s->chroma_height); v_.resize(u_.size());
  std::memcpy(y_.data(), info->display_fbuf->buf[0], y_.size());
  std::memcpy(u_.data(), info->display_fbuf->buf[1], u_.size());
  std::memcpy(v_.data(), info->display_fbuf->buf[2], v_.size());
  lumaStride_ = stride;
  if (s->frame_period) framePeriod_ = s->frame_period;
  ++serial_; ++decodedSerial_;
}

InputPicture Mpeg2Video::picture() const {
  InputPicture out;
  if (!serial_ || !width_ || !height_ || !lumaStride_) return out;
  out.width = width_; out.height = height_; out.bgra.resize(size_t(width_) * height_);
  const unsigned xs = lumaStride_ / std::max(1u, chromaWidth_), ys = (unsigned(y_.size() / lumaStride_)) / std::max(1u, chromaHeight_);
  auto clamp = [](int v) { return uint32_t(v < 0 ? 0 : v > 255 ? 255 : v); };
  for (unsigned y = 0; y < height_; ++y)
    for (unsigned x = 0; x < width_; ++x) {
      const int Y = y_[size_t(y) * lumaStride_ + x] - 16;
      const size_t c = size_t(y / std::max(1u, ys)) * chromaWidth_ + x / std::max(1u, xs);
      const int U = u_[c] - 128, V = v_[c] - 128;
      // BT.601 limited range (MPEG-2 default)
      const int r = (298 * Y + 409 * V + 128) >> 8, g = (298 * Y - 100 * U - 208 * V + 128) >> 8, b = (298 * Y + 516 * U + 128) >> 8;
      out.bgra[size_t(y) * width_ + x] = 0xff000000u | clamp(r) << 16 | clamp(g) << 8 | clamp(b);
    }
  return out;
}
}
