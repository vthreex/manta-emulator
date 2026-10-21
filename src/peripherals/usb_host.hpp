#pragma once
// EXPLORATORY USB host model: a Faraday-FOTG210-style EHCI controller (the register layout the original driver uses: USBCMD 0x10,
// USBSTS 0x14, USBINTR 0x18, FRINDEX 0x1C, PERIODICLISTBASE 0x24, ASYNCLISTADDR 0x28, PORTSC 0x30; see docs/usb-pendrive.md) plus one
// bulk-only SCSI mass-storage device backed by a disk image. Everything here is opt-in (--exploratory-usb-pendrive) and labelled:
// the MStar UHC reset values, port-reset duration and speed signalling are NOT Titania4-verified. The queue/qTD processing follows the
// public EHCI structures that the original driver builds in DRAM (QH info1 bit fields and qTD tokens decode the same way).
#include "snapshot/archive.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace manta {

class UsbMassStorage {
public:
  explicit UsbMassStorage(std::vector<uint8_t> image) : image_(std::move(image)) {
    image_.resize((image_.size() + 511) / 512 * 512);
    uint64_t h = 1469598103934665603ull;   // FNV-1a over the pristine image: a restored snapshot must be paired with the same file
    for (uint8_t b : image_) h = (h ^ b) * 1099511628211ull;
    imageHash_ = h;
  }
  // The image file itself is never modified; guest WRITE(10) blocks live only in memory and are saved in snapshots (dirty blocks only).
  void serialize(snap::Ar& a) {
    a.tag("UsbMassStorage");
    uint64_t hash = imageHash_; a(hash);
    if (a.loading() && hash != imageHash_) throw snap::SnapshotError("snapshot was taken with a different USB pendrive image");
    uint8_t bot = uint8_t(bot_), stage = uint8_t(ctlStage_);
    a(address_, config_, ctl_, inq_, outBuf_, ctlOffset_, inqOffset_, outExpected_, ctlDeviceToHost_, ctlStall_, pendingAddress_, tag_, residue_, writeLba_,
      csStatus_, senseKey_, asc_, bot, stage, commands, readBlocks, writtenBlocks, log);
    if (a.loading()) { bot_ = Bot(bot); ctlStage_ = Stage(stage); }
    uint64_t n = a.count(dirty_.size());
    if (a.saving()) {
      for (uint32_t lba : dirty_) { uint32_t l = lba; a(l); a.raw(&image_[size_t(lba) * 512], 512); }
    } else {
      dirty_.clear();
      for (uint64_t i = 0; i < n; ++i) { uint32_t l = 0; a(l); if (uint64_t(l) >= blocks()) throw snap::SnapshotError("snapshot USB block out of range"); a.raw(&image_[size_t(l) * 512], 512); dirty_.insert(l); }
    }
  }
  uint64_t blocks() const { return image_.size() / 512; }
  uint8_t address() const { return address_; }
  void busReset() { address_ = 0; config_ = 0; ctl_.clear(); ctlStage_ = None; bot_ = BotIdle; inq_.clear(); }

  // ---- control endpoint 0 ----
  void setup(const uint8_t s[8]) {
    ctl_.clear(); ctlOffset_ = 0; ctlStage_ = None; pendingAddress_ = -1;
    uint8_t type = s[0], req = s[1];
    uint16_t value = uint16_t(s[2] | s[3] << 8), index = uint16_t(s[4] | s[5] << 8), length = uint16_t(s[6] | s[7] << 8);
    (void)index;
    ctlDeviceToHost_ = (type & 0x80) != 0;
    if ((type & 0x60) == 0) {  // standard
      switch (req) {
      case 0x06: {  // GET_DESCRIPTOR
        uint8_t kind = uint8_t(value >> 8), idx = uint8_t(value);
        if (kind == 1) ctl_ = deviceDescriptor();
        else if (kind == 2) ctl_ = configDescriptor();
        else if (kind == 3) ctl_ = stringDescriptor(idx);
        else if (kind == 6) { ctlStall_ = true; }   // device qualifier: full-speed only device answers STALL
        else ctlStall_ = true;
        if (ctl_.size() > length) ctl_.resize(length);
        break;
      }
      case 0x05: pendingAddress_ = int(value & 0x7f); break;      // SET_ADDRESS (applied after the status stage)
      case 0x09: config_ = uint8_t(value); break;                  // SET_CONFIGURATION
      case 0x08: ctl_ = {config_}; break;                          // GET_CONFIGURATION
      case 0x00: ctl_ = {0, 0}; break;                             // GET_STATUS
      case 0x01: case 0x03: case 0x0b: break;                      // CLEAR/SET_FEATURE, SET_INTERFACE: accepted
      case 0x0a: ctl_ = {0}; break;                                // GET_INTERFACE
      default: ctlStall_ = true; break;
      }
    } else if ((type & 0x60) == 0x20) {  // class
      if (req == 0xfe) ctl_ = {0};                                 // GET_MAX_LUN = 0
      else if (req == 0xff) {}                                     // BOT mass-storage reset
      else ctlStall_ = true;
    } else ctlStall_ = true;
    ctlStage_ = Setup;
  }
  bool takeStall() { bool s = ctlStall_; ctlStall_ = false; return s; }
  std::vector<uint8_t> controlIn(size_t maxPacket) {   // data stage IN packet
    size_t n = std::min(maxPacket, ctl_.size() - ctlOffset_);
    std::vector<uint8_t> out(ctl_.begin() + ctlOffset_, ctl_.begin() + ctlOffset_ + n);
    ctlOffset_ += n;
    return out;
  }
  void controlStatus() {   // status stage completed
    ctlOffset_ = 0;
    if (pendingAddress_ >= 0) { address_ = uint8_t(pendingAddress_); pendingAddress_ = -1; }
  }
  void controlSetupDone() { ctlOffset_ = 0; }

  // ---- bulk endpoints: IN 0x81, OUT 0x02 ----
  void bulkOut(std::span<const uint8_t> data) {
    if (bot_ == BotIdle || bot_ == BotStatus) {
      if (data.size() >= 31 && data[0] == 'U' && data[1] == 'S' && data[2] == 'B' && data[3] == 'C') startCommand(data);
      return;
    }
    if (bot_ == BotDataOut) {
      outBuf_.insert(outBuf_.end(), data.begin(), data.end());
      if (outBuf_.size() >= outExpected_) finishWrite();
    }
  }
  std::vector<uint8_t> bulkIn(size_t maxPacket) {
    if (inq_.empty()) return {};
    size_t n = std::min(maxPacket, inq_.size() - inqOffset_);
    std::vector<uint8_t> out(inq_.begin() + inqOffset_, inq_.begin() + inqOffset_ + n);
    inqOffset_ += n;
    if (inqOffset_ >= inq_.size()) { inq_.clear(); inqOffset_ = 0; if (bot_ == BotDataIn) queueStatus(); else bot_ = BotIdle; }
    return out;
  }
  bool bulkInReady() const { return !inq_.empty(); }
  uint64_t commands = 0, readBlocks = 0, writtenBlocks = 0;
  std::vector<std::string> log;

private:
  enum Stage { None, Setup } ctlStage_ = None;
  enum Bot { BotIdle, BotDataIn, BotDataOut, BotStatus } bot_ = BotIdle;
  std::vector<uint8_t> image_, ctl_, inq_, outBuf_;
  std::set<uint32_t> dirty_;
  uint64_t imageHash_ = 0;
  size_t ctlOffset_ = 0, inqOffset_ = 0, outExpected_ = 0;
  bool ctlDeviceToHost_ = true, ctlStall_ = false;
  int pendingAddress_ = -1;
  uint8_t address_ = 0, config_ = 0;
  uint32_t tag_ = 0, residue_ = 0, writeLba_ = 0;
  uint8_t csStatus_ = 0, senseKey_ = 0, asc_ = 0;

  static void put16(std::vector<uint8_t>& v, unsigned x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
  std::vector<uint8_t> deviceDescriptor() const {
    // bcdUSB 2.00, class defined at the interface, ep0 max packet 64, VID/PID of the open-source test range (pid.codes 0x1209:0x0001
    // is documented as "internal testing"), bcdDevice 1.00, strings 1/2/3, one configuration.
    std::vector<uint8_t> d = {18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 1, 2, 3, 1};
    return d;
  }
  std::vector<uint8_t> configDescriptor() const {
    std::vector<uint8_t> d = {9, 2, 32, 0, 1, 1, 0, 0x80, 50,        // configuration (bus powered, 100 mA)
                              9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,      // interface: mass storage, SCSI transparent, bulk-only
                              7, 5, 0x81, 2, 0x00, 0x02, 0,            // bulk IN, 512
                              7, 5, 0x02, 2, 0x00, 0x02, 0};           // bulk OUT, 512
    return d;
  }
  std::vector<uint8_t> stringDescriptor(unsigned idx) const {
    if (idx == 0) return {4, 3, 0x09, 0x04};
    std::string s = idx == 1 ? "Manta Emulator" : idx == 2 ? "Virtual Pendrive" : "000000000001";
    std::vector<uint8_t> d = {uint8_t(2 + 2 * s.size()), 3};
    for (char c : s) { d.push_back(uint8_t(c)); d.push_back(0); }
    return d;
  }

  void queueStatus() {
    inq_.clear(); inqOffset_ = 0;
    std::vector<uint8_t> csw = {'U', 'S', 'B', 'S'};
    for (int i = 0; i < 4; ++i) csw.push_back(uint8_t(tag_ >> (8 * i)));
    for (int i = 0; i < 4; ++i) csw.push_back(uint8_t(residue_ >> (8 * i)));
    csw.push_back(csStatus_);
    inq_ = csw;
    bot_ = BotStatus;
  }
  void dataIn(std::vector<uint8_t> v, uint32_t expected) {
    if (v.size() > expected) v.resize(expected);
    residue_ = expected - uint32_t(v.size());
    if (v.empty()) { queueStatus(); return; }
    inq_ = std::move(v); inqOffset_ = 0; bot_ = BotDataIn;
  }
  void fail(uint8_t key, uint8_t asc, uint32_t expected) {
    csStatus_ = 1; senseKey_ = key; asc_ = asc; residue_ = expected;
    queueStatus();
  }
  void startCommand(std::span<const uint8_t> c) {
    ++commands;
    tag_ = uint32_t(c[4] | c[5] << 8 | c[6] << 16 | uint32_t(c[7]) << 24);
    uint32_t len = uint32_t(c[8] | c[9] << 8 | c[10] << 16 | uint32_t(c[11]) << 24);
    bool toHost = (c[12] & 0x80) != 0;
    const uint8_t* cb = &c[15];
    csStatus_ = 0; residue_ = 0;
    char buf[96];
    if (cb[0] == 0x28 || cb[0] == 0x2a) std::snprintf(buf, sizeof buf, "scsi %02x lba=%u blocks=%u", cb[0], uint32_t(cb[2] << 24 | cb[3] << 16 | cb[4] << 8 | cb[5]), uint32_t(cb[7] << 8 | cb[8]));
    else std::snprintf(buf, sizeof buf, "scsi %02x len=%u %s", cb[0], len, toHost ? "in" : "out");
    if (log.size() < 600) log.push_back(buf);
    switch (cb[0]) {
    case 0x00: if (len) fail(5, 0x24, len); else queueStatus(); break;   // TEST UNIT READY
    case 0x03: {                                                           // REQUEST SENSE
      std::vector<uint8_t> s(18, 0); s[0] = 0x70; s[2] = senseKey_; s[7] = 10; s[12] = asc_;
      senseKey_ = 0; asc_ = 0; dataIn(s, len); break; }
    case 0x12: {                                                           // INQUIRY
      std::vector<uint8_t> s(36, 0);
      s[0] = 0x00; s[1] = 0x80; s[2] = 0x04; s[3] = 0x02; s[4] = 31;
      std::memcpy(&s[8], "MANTA   ", 8); std::memcpy(&s[16], "EMU PENDRIVE    ", 16); std::memcpy(&s[32], "1.00", 4);
      dataIn(s, len); break; }
    case 0x1a: { std::vector<uint8_t> s = {3, 0, 0, 0}; dataIn(s, len); break; }                  // MODE SENSE(6): not write protected
    case 0x5a: { std::vector<uint8_t> s = {0, 6, 0, 0, 0, 0, 0, 0}; dataIn(s, len); break; }      // MODE SENSE(10)
    case 0x1b: case 0x1e: queueStatus(); break;                                                    // START STOP / PREVENT ALLOW
    case 0x25: {                                                                                   // READ CAPACITY(10)
      uint32_t last = uint32_t(blocks() - 1);
      std::vector<uint8_t> s = {uint8_t(last >> 24), uint8_t(last >> 16), uint8_t(last >> 8), uint8_t(last), 0, 0, 2, 0};
      dataIn(s, len); break; }
    case 0x23: {                                                                                   // READ FORMAT CAPACITIES
      uint32_t n = uint32_t(blocks());
      std::vector<uint8_t> s = {0, 0, 0, 8, uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n), 2, 0, 2, 0};
      dataIn(s, len); break; }
    case 0x28: {                                                                                   // READ(10)
      uint32_t lba = uint32_t(cb[2] << 24 | cb[3] << 16 | cb[4] << 8 | cb[5]), n = uint32_t(cb[7] << 8 | cb[8]);
      if (uint64_t(lba) + n > blocks()) { fail(5, 0x21, len); break; }
      readBlocks += n;
      dataIn(std::vector<uint8_t>(image_.begin() + size_t(lba) * 512, image_.begin() + size_t(lba + n) * 512), len);
      break; }
    case 0x2a: {                                                                                   // WRITE(10): kept in memory only
      uint32_t lba = uint32_t(cb[2] << 24 | cb[3] << 16 | cb[4] << 8 | cb[5]), n = uint32_t(cb[7] << 8 | cb[8]);
      if (uint64_t(lba) + n > blocks()) { fail(5, 0x21, len); break; }
      writeLba_ = lba; outExpected_ = size_t(n) * 512; outBuf_.clear();
      if (outExpected_ == 0) { queueStatus(); break; }
      bot_ = BotDataOut; break; }
    default: fail(5, 0x20, len); break;                                                            // ILLEGAL REQUEST: invalid opcode
    }
  }
  void finishWrite() {
    std::copy(outBuf_.begin(), outBuf_.begin() + outExpected_, image_.begin() + size_t(writeLba_) * 512);
    for (size_t i = 0; i < outExpected_ / 512; ++i) dirty_.insert(writeLba_ + uint32_t(i));
    writtenBlocks += outExpected_ / 512; outBuf_.clear(); queueStatus();
  }
};

class UsbHost {
public:
  struct Bus {   // physical (MIU) DRAM access supplied by the machine
    std::function<std::vector<uint8_t>(uint32_t, size_t)> read;
    std::function<void(uint32_t, std::span<const uint8_t>)> write;
    std::function<void(const std::string&, uint32_t, uint32_t, const std::string&)> event;
    std::function<bool(unsigned)> phyPowered;     // guest powered the port-0 PHY
    std::function<uint32_t(uint32_t)> storedReg;   // plain stored UHC register dword (guest-written), byte offset
    std::function<bool(uint32_t)> storedKnown;
  };
  static constexpr uint32_t kBase = 0x1f204800u, kSize = 0x200u;

  void attach(std::vector<uint8_t> image) { dev_ = std::make_unique<UsbMassStorage>(std::move(image)); }
  bool enabled() const { return dev_ != nullptr; }
  UsbMassStorage* device() const { return dev_.get(); }
  Bus bus;
  uint64_t transactions = 0, qtdsRetired = 0, scans = 0;
  std::vector<std::string> trace;   // first 400 notable events (setup packets, resets, SCSI commands) for the run summary
  void note(const std::string& kind, uint32_t a, uint32_t n, const std::string& d) {
    if (trace.size() < 400) trace.push_back(kind + " a=" + std::to_string(a) + " n=" + std::to_string(n) + " " + d);
    if (bus.event) bus.event(kind, a, n, d);
  }
  static bool inRange(uint32_t a, unsigned n) { return a + n > kBase && a < kBase + kSize; }

  // RIU address -> UHC byte index (the register file is 16-bit registers on a 4-byte stride); -1 for the unused half of each word
  static int index(uint32_t a) {
    uint32_t p = a - kBase;
    if (p & 2) return -1;
    return int((p >> 2) * 2 + (p & 1));
  }
  // Model-owned registers: capability (0x00..0x0B), USBCMD, USBSTS, FRINDEX, PORTSC. Everything else is plain storage in the RIU.
  static bool owned(int idx) { return (idx >= 0 && idx < 0x0c) || (idx >= 0x10 && idx < 0x18) || (idx >= 0x1c && idx < 0x20) || (idx >= 0x30 && idx < 0x34) || (idx >= 0x34 && idx < 0x50); }
  bool owns(uint32_t a, unsigned n) const {
    for (unsigned i = 0; i < n; ++i) if (owned(index(a + i))) return true;
    return false;
  }

  uint32_t read(uint32_t a, unsigned n, uint64_t tick) {
    advance(tick);
    uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i) {
      int idx = index(a + i);
      uint32_t b = idx < 0 ? 0 : (owned(idx) ? readByte(idx, tick) : 0);
      v |= b << (8 * i);
    }
    return v;
  }
  void write(uint32_t a, unsigned n, uint32_t v, uint64_t tick) {
    advance(tick);
    for (unsigned i = 0; i < n; ++i) {
      int idx = index(a + i);
      if (idx >= 0 && owned(idx)) writeByte(idx, uint8_t(v >> (8 * i)), tick);
    }
    if (n >= 1) advance(tick, true);
  }

  // Called with every RIU access (cheap): runs the asynchronous schedule when the controller is running.
  void advance(uint64_t tick, bool force = false) {
    if (!dev_) return;
    attachCheck(tick);
    if (portReset_ && tick >= resetEnd_) finishReset(tick);
    if (!(cmd_ & 1) || !(cmd_ & 0x20)) return;
    if (!force && tick - lastScan_ < 64) return;
    lastScan_ = tick;
    scanAsync(tick);
  }
  void serialize(snap::Ar& a) {
    a.tag("UsbHost");
    a(cmd_, sts_, intr_, portsc_, attached_, portReset_, portEnabled_, store_, hostSpeed_, resetEnd_, lastScan_, attachTick_, transactions, qtdsRetired, scans, trace);
    if (dev_) dev_->serialize(a);
  }
  uint32_t cmd() const { return cmd_; }
  uint32_t status() const { return sts_; }

private:
  std::unique_ptr<UsbMassStorage> dev_;
  uint32_t cmd_ = 0x00080000, sts_ = 0, intr_ = 0, portsc_ = 0x1000;
  bool attached_ = false, portReset_ = false, portEnabled_ = false;
  std::array<uint8_t, 0x100> store_{};
  uint8_t hostSpeed_ = 0;   // hostSpeed_: 0 unknown/none, 2 = high-speed (log string "Host type:%x" of the original driver)
  uint64_t resetEnd_ = 0, lastScan_ = 0, attachTick_ = 0;
  static constexpr uint64_t kResetTicks = 20000;   // provisional port-reset duration (guest ticks)

  void attachCheck(uint64_t tick) {
    if (attached_ || !bus.phyPowered || !bus.phyPowered(0)) return;
    attached_ = true; attachTick_ = tick;
    portsc_ |= 0x3;   // CCS | CSC
    sts_ |= 0x4;      // port change detect
    note("usb_pendrive_attach", 0, 0, "EXPLORATORY: virtual mass-storage device connected once the guest powered the port-0 PHY");
  }
  void finishReset(uint64_t) {
    portReset_ = false; portsc_ &= ~0x100u;
    portEnabled_ = true; portsc_ |= 0x4u; portsc_ |= 0x8u;   // PE (+PEC)
    hostSpeed_ = 2;
    portsc_ &= ~0xC00u;                                       // high-speed device after the chirp: line status SE0
    if (dev_) dev_->busReset();
    sts_ |= 0x4;
    note("usb_port_reset_done", 0, 0, "EXPLORATORY: provisional reset duration; high-speed enumeration assumed");
  }
  uint32_t portscValue() const {
    uint32_t v = portsc_ & ~0x1u;
    if (attached_) v |= 1u;
    if (!attached_) v &= ~0x6u;
    if (attached_ && !portEnabled_ && !portReset_) v = (v & ~0xC00u) | 0x800u;   // J-state before the reset
    return v;
  }
  uint8_t readByte(int idx, uint64_t tick) {
    if (idx >= 0x34 && idx < 0x50) {   // MStar UHC extension block: plain storage (reset 0 assumed); 0x41 bits 2:1 = detected host speed
      uint8_t v = store_[idx];
      if (idx == 0x41) v = uint8_t((v & ~0x06) | (hostSpeed_ << 1));
      return v;
    }
    uint32_t reg = idx & ~3, sh = 8 * (idx & 3), val = 0;
    switch (reg) {
    case 0x00: val = 0x01000010u; break;   // CAPLENGTH = 0x10, HCIVERSION = 0x0100 (FOTG210 layout)
    case 0x04: val = 0x00000001u; break;   // one port
    case 0x08: val = 0; break;
    case 0x10: val = cmd_ & ~0x2u; break;
    case 0x14: {
      uint32_t s = sts_ & 0x3f;
      if (!(cmd_ & 1)) s |= 0x1000;
      if ((cmd_ & 1) && (cmd_ & 0x20)) s |= 0x8000;
      if ((cmd_ & 1) && (cmd_ & 0x10)) s |= 0x4000;
      val = s; break; }
    case 0x1c: val = uint32_t(tick / 125) & 0x3fff; break;
    case 0x30: val = portscValue(); break;
    default: val = 0; break;
    }
    return uint8_t(val >> sh);
  }
  void writeByte(int idx, uint8_t b, uint64_t tick) {
    if (idx >= 0x34 && idx < 0x50) { store_[idx] = b; return; }
    uint32_t reg = idx & ~3, sh = 8 * (idx & 3);
    auto merge = [&](uint32_t old) { return (old & ~(0xffu << sh)) | (uint32_t(b) << sh); };
    switch (reg) {
    case 0x10: {
      uint32_t n = merge(cmd_);
      if (n & 0x2) {   // HCRESET: operational state returns to its defaults; the device stays plugged in
        cmd_ = 0x00080000; sts_ = 0; portsc_ = 0x1000; portReset_ = false; portEnabled_ = false; hostSpeed_ = 0;
        if (attached_) { portsc_ |= 0x3; sts_ |= 0x4; }
        if (dev_) dev_->busReset();
        break;
      }
      if (n & 0x40) { sts_ |= 0x20; n &= ~0x40u; }   // IAAD doorbell: advance acknowledged at the next scan
      cmd_ = n; break; }
    case 0x14: sts_ &= ~(uint32_t(b) << sh) | ~0x3fu; break;   // write-1-clear (INT, ERR, PCD, FLR, FATAL, IAA)
    case 0x30: {
      uint32_t w = uint32_t(b) << sh;
      if (sh == 0) {
        if (w & 0x2) portsc_ &= ~0x2u;      // CSC write-1-clear
        if (w & 0x8) portsc_ &= ~0x8u;      // PEC write-1-clear
        if (!(w & 0x4)) { if (portEnabled_) { portEnabled_ = false; portsc_ &= ~0x4u; } }
        portsc_ = (portsc_ & ~0xc0u) | (w & 0xc0u);
      } else if (sh == 8) {
        bool reset = (w & 0x100) != 0;
        if (reset && !portReset_ && attached_) {
          portReset_ = true; resetEnd_ = tick + kResetTicks; portEnabled_ = false; portsc_ &= ~0x4u; portsc_ |= 0x100u;
          note("usb_port_reset_start", 0, 0, "guest asserted PORTSC.PR");
        } else if (!reset && portReset_) finishReset(tick);
        portsc_ = (portsc_ & ~0x1000u) | (w & 0x1000);   // port power
      }
      break; }
    case 0x1c: break;
    default: break;
    }
  }

  // ----- EHCI queue processing -----
  static uint32_t rd32(const std::vector<uint8_t>& v, size_t off) { return uint32_t(v[off] | v[off + 1] << 8 | v[off + 2] << 16 | uint32_t(v[off + 3]) << 24); }
  static void put32(std::vector<uint8_t>& v, size_t off, uint32_t x) { for (int i = 0; i < 4; ++i) v[off + i] = uint8_t(x >> (8 * i)); }

  void scanAsync(uint64_t tick) {
    ++scans;
    uint32_t head = bus.storedReg(0x28);
    if (!head) { finishAdvance(); return; }
    uint32_t qh = head & ~0x1fu;
    for (int guard = 0; guard < 32; ++guard) {
      auto q = bus.read(qh, 48);
      processQueue(qh, q, tick);
      uint32_t next = rd32(q, 0);
      if (next & 1) break;
      next &= ~0x1fu;
      if (next == (head & ~0x1fu)) break;
      qh = next;
    }
    finishAdvance();
  }
  void finishAdvance() { }

  void processQueue(uint32_t qhAddr, std::vector<uint8_t>& q, uint64_t tick) {
    uint32_t info1 = rd32(q, 4);
    for (int guard = 0; guard < 64; ++guard) {
      uint32_t overlayTok = rd32(q, 0x18);
      if (overlayTok & 0x40) return;                       // halted
      uint32_t nextQtd = rd32(q, 0x10);
      if (nextQtd & 1) return;                             // terminate: nothing queued
      uint32_t qtdAddr = nextQtd & ~0x1fu;
      auto t = bus.read(qtdAddr, 32);
      uint32_t tok = rd32(t, 8);
      if (!(tok & 0x80)) return;                           // inactive qTD (the driver's dummy): leave QH.qtd_next pointing at it so the driver can fill it in place
      uint32_t shortNext = rd32(t, 4);
      bool stop = false;
      uint32_t newTok = executeQtd(info1, t, tok, shortNext, stop, tick);
      put32(t, 8, newTok);
      bus.write(qtdAddr + 8, std::span<const uint8_t>(t.data() + 8, 4));
      ++qtdsRetired;
      uint32_t follow = (stop && !(shortNext & 1)) ? shortNext : rd32(t, 0);
      put32(q, 0x0c, qtdAddr); put32(q, 0x10, follow); put32(q, 0x18, newTok & ~0x80u);
      bus.write(qhAddr + 0x0c, std::span<const uint8_t>(q.data() + 0x0c, 16));
      if (newTok & 0x40) { sts_ |= 0x2; return; }          // halted: error interrupt
      if (newTok & 0x8000) sts_ |= 0x1;                    // IOC
    }
  }

  // Returns the new qTD token. `stop` = short packet on an IN transfer (follow the alternate pointer).
  uint32_t executeQtd(uint32_t info1, std::vector<uint8_t>& t, uint32_t tok, uint32_t, bool& stop, uint64_t) {
    unsigned devAddr = info1 & 0x7f, ep = (info1 >> 8) & 0xf, maxPkt = (info1 >> 16) & 0x7ff;
    unsigned pid = (tok >> 8) & 3;                         // 0 OUT, 1 IN, 2 SETUP
    uint32_t total = (tok >> 16) & 0x7fff, remaining = total;
    uint32_t toggle = tok >> 31;
    if (maxPkt == 0) maxPkt = 64;
    if (!dev_ || devAddr != dev_->address() || !portEnabled_) {
      note("usb_no_device_at_address", devAddr, ep, "transaction to an address nobody answers (Transaction Error)");
      return (tok & ~0xffu & ~0x80u) | 0x48;
    }
    // buffer cursor over hw_buf[0..4]
    uint32_t cur = rd32(t, 0x0c), bufIdx = 0;
    auto bufAddr = [&](uint32_t off) {   // byte `off` into the transfer
      uint32_t abs = (cur & 0xfffu) + off;
      uint32_t page = abs >> 12; if (page > 4) return 0u;
      uint32_t base = page == 0 ? (rd32(t, 0x0c) & ~0xfffu) : (rd32(t, 0x0c + 4 * page) & ~0xfffu);
      return base + (abs & 0xfffu);
    };
    (void)bufIdx;
    auto readOut = [&](uint32_t off, uint32_t len) {
      std::vector<uint8_t> out; out.reserve(len);
      while (len) { uint32_t a = bufAddr(off); uint32_t chunk = std::min(len, 0x1000u - (a & 0xfffu)); auto b = bus.read(a, chunk); out.insert(out.end(), b.begin(), b.end()); off += chunk; len -= chunk; }
      return out;
    };
    auto writeIn = [&](uint32_t off, const std::vector<uint8_t>& d) {
      size_t i = 0;
      while (i < d.size()) { uint32_t a = bufAddr(off + uint32_t(i)); size_t chunk = std::min<size_t>(d.size() - i, 0x1000u - (a & 0xfffu)); bus.write(a, std::span<const uint8_t>(d.data() + i, chunk)); i += chunk; }
    };
    ++transactions;
    uint32_t done = 0;
    bool controlEp = ep == 0;
    if (pid == 2) {   // SETUP
      auto s = readOut(0, 8);
      dev_->setup(s.data());
      char b[96]; std::snprintf(b, sizeof b, "SETUP %02x %02x v=%02x%02x i=%02x%02x l=%02x%02x", s[0], s[1], s[3], s[2], s[5], s[4], s[7], s[6]);
      note("usb_setup", devAddr, ep, b);
      remaining = total >= 8 ? total - 8 : 0;
      if (dev_->takeStall()) return (tok & ~0xffu & ~0x80u) | 0x40;
      toggle ^= 1;
    } else if (controlEp) {
      if (pid == 1) {   // IN
        while (remaining > 0 || total == 0) {
          auto p = dev_->controlIn(maxPkt);
          writeIn(done, p); done += uint32_t(p.size()); remaining = total > done ? total - done : 0; toggle ^= 1;
          if (p.size() < maxPkt) { if (total == 0) dev_->controlStatus(); stop = p.size() < maxPkt && remaining > 0; break; }
          if (remaining == 0) break;
        }
        if (total == 0) dev_->controlStatus();
      } else {          // OUT (data or status)
        auto d = readOut(0, total); (void)d; remaining = 0; toggle ^= 1; dev_->controlStatus();
      }
    } else if (ep == 1 && pid == 1) {   // bulk IN
      while (remaining > 0) {
        auto p = dev_->bulkIn(maxPkt);
        if (p.empty() && !dev_->bulkInReady()) { break; }   // NAK: leave the qTD active, retry on the next scan
        writeIn(done, p); done += uint32_t(p.size()); remaining = total - done; toggle ^= 1;
        if (p.size() < maxPkt) { stop = remaining > 0; break; }
      }
      if (remaining > 0 && !stop) return tok;   // NAK (device has no data queued yet): the qTD stays active and is retried
    } else if (ep == 2 && pid == 0) {   // bulk OUT
      uint32_t off = 0;
      while (off < total) { uint32_t len = std::min<uint32_t>(maxPkt, total - off); dev_->bulkOut(readOut(off, len)); off += len; toggle ^= 1; }
      remaining = 0;
    } else {
      note("usb_unsupported_endpoint", ep, pid, "no such endpoint on the virtual pendrive");
      return (tok & ~0xffu & ~0x80u) | 0x48;
    }
    uint32_t out = (tok & ~0xffu & ~0x7fff0000u & ~0x80000000u) | (remaining << 16) | (toggle << 31);
    return out;   // active bit cleared, status 0
  }
};

}  // namespace manta
