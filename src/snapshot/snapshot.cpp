// Full-machine save state: one visitor (ioClass) per stateful class plus the machine-level container format.
// See docs/save-state.md for the design, what is and is not part of a snapshot, and the safe-boundary rules.
#include "board/machine.hpp"
#include "peripherals/riu.hpp"
#include "snapshot/archive.hpp"
#include "storage/sha256.hpp"
#include <cstdio>
#include <fstream>
#include <unordered_map>
namespace manta {
using snap::SnapshotError;
using snap::SnapshotUnsupported;

// ---------------------------------------------------------------------------------------------------------------- plain structs
void ioClass(Ar& a, RiuByte& v) { a(v.value, v.known); }
// Same byte stream as the former std::map<uint32_t,RiuByte>: element count, then ascending (key, value) pairs.
void ioClass(Ar& a, RiuByteMap& m) {
  uint64_t n = a.count(m.size());
  if (a.saving()) {
    m.forEach([&](uint32_t k, const RiuByte& v) { uint32_t kk = k; RiuByte vv = v; a(kk); ioClass(a, vv); });
  } else {
    m.clear();
    for (uint64_t i = 0; i < n; ++i) { uint32_t k = 0; RiuByte v{}; a(k); ioClass(a, v); m[k] = v; }
  }
}
void ioClass(Ar& a, VdByte& v) { a(v.value, v.known, v.origin); }
void ioClass(Ar& a, OrWord& v) { a(v.value, v.known, v.origin); }
void ioClass(Ar& a, PanelTiming& v) { a(v.htotal, v.vtotal, v.hstart, v.hend, v.vstart, v.vend, v.vsyncStart, v.vsyncEnd, v.invert); }
void ioClass(Ar& a, TspQueue::FileIn& v) { a(v.address, v.size, v.control, v.tick); }
void ioClass(Ar& a, VifZeroInput::Field& v) { a(v.value, v.known); }
void ioClass(Ar& a, Riu::GopLogEntry& v) { a(v.tick, v.pc, v.kind, v.bank, v.offset, v.value, v.n); }
void ioClass(Ar& a, ZuiFlowSnapshot& s) {
  a(s.pc, s.tick, s.state, s.previous, s.active, s.head, s.tail, s.windowCount, s.windowList, s.softwareTick, s.wrapperState, s.inputServiceState,
    s.inputKey, s.sourceId, s.sourcePhase, s.sourceTimeoutTimestamp, s.softwareFlags, s.bitmap196Address, s.bitmap196Bytes, s.bitmap196Pitch,
    s.bitmap196Width, s.bitmap196Height, s.bitmap196Format, s.bitmap196Valid, s.surfaceFlags, s.geContextPointer, s.geStaticSignature,
    s.geStaticFormat, s.geStaticDestinationLow, s.geStaticDestinationHigh, s.geStaticDestinationPitch, s.bitmap196Sha256, s.knownSlots,
    s.occupiedSlots, s.indicesValid);
}
void ioClass(Ar& a, Msd309Machine::WatchHit& h) {
  a(h.dispatch);
  for (auto& r : h.reg) a(r);
  for (auto& s : h.stack) a(s);
}
void ioClass(Ar& a, Msd309Machine::IrLogEntry& e) { a(e.dispatch, e.function, e.raw, e.accepted, e.note); }
void ioClass(Ar& a, Msd309Machine::Poll& p) { a(p.value, p.epoch, p.count); }
void ioClass(Ar& a, Msd309Machine::Instruction& i) { a(i.tick, i.pc, i.word, i.ra, i.sp); }

// ------------------------------------------------------------------------------------------------------------------ peripherals
void ioClass(Ar& a, GpioI2c& g) {
  a.tag("GpioI2c");
  for (auto& b : g.buses_)
    a(b.input, b.output, b.oen, b.sclMask, b.sdaMask, b.out, b.outKnown, b.direction, b.directionKnown, b.valid, b.scl, b.sda, b.active, b.bits, b.bytes,
      b.shift, b.externalLow);
}
void ioClass(Ar& a, SpiNor& n) {
  a.tag("SpiNor");
  a(n.packet_, n.outputIndex_, n.selected_, n.wel_, n.asleep_, n.busyUntil_, n.sr_, n.srKnown_, n.model, n.unknownInput, n.error, n.undocumentedSamples,
    n.now, n.programTicks, n.erase4kTicks, n.erase32kTicks, n.erase64kTicks, n.writeSupport, n.statusWriteTicks, n.statusWrites, n.blockedByProtection,
    n.programs, n.erases, n.ignoredWrites, n.norViolations, n.programmedBytes);
}
void ioClass(Ar& a, SpiIsp& s) {
  a.tag("SpiIsp");
  a(s.clockConfigured_, s.enabled_, s.trigger_, s.readPending_, s.commandReady_, s.writeReady_, s.readReady_, s.input_, s.inputKnown_, s.address_,
    s.lastTick_, s.shiftEnd_, s.commandSession_, s.nor, s.error);
}
void ioClass(Ar& a, HardwareSemaphore& s) { a.tag("Semaphore"); a(s.owners_, s.acquisitions_, s.releases_, s.contentions_); }
void ioClass(Ar& a, GammaRam& g) { a.tag("Gamma"); a(g.busy_, g.address_, g.sample_, g.channel_, g.ready_, g.data, g.written, g.commits); }
void ioClass(Ar& a, ScalerTiming& s) { a.tag("Scaler"); a(s.timing_, s.origin_, s.hz_, s.enabled_); }
void ioClass(Ar& a, GeQueue& g) {
  a.tag("GeQueue");
  if (a.saving() && (!g.submission_.empty() || !g.execution_.empty()))
    throw SnapshotUnsupported("GE queue holds pending raster closures; a snapshot is only valid at an idle GE queue");
  if (a.loading()) { g.submission_.clear(); g.execution_.clear(); }
  a(g.initialized_, g.running_, g.submitted_, g.completed_, g.transferred_, g.tick_, g.cursor_, g.completedTag_);
}
void ioClass(Ar& a, GopPalette& p) {
  a.tag("GopPalette");
  a(p.colors_, p.modes_, p.commits_);
  for (auto& o : p.pending_) {
    bool has = o.has_value();
    a(has);
    if (a.loading()) { if (has) o = GopPalette::Pending{}; else o.reset(); }
    if (has) a(o->index, o->color, o->mode);
  }
}
void ioClass(Ar& a, GopUpdate& g) { a.tag("GopUpdate"); a(g.normalHigh_, g.forceHigh_, g.pending_, g.known_, g.busy_, g.requestedEdge_, g.requests, g.forced, g.latches); }
void ioClass(Ar& a, TspQueue& t) { a.tag("TspQueue"); a(t.pending_, t.resetKnown_, t.held_, t.experimentalResetLow_, t.resets_, t.releases_, t.submissions_); }
void ioClass(Ar& a, TspDownload& t) { a.tag("TspDownload"); a(t.qmem_, t.valid_, t.payload_, t.due_, t.requests_, t.completions_, t.pending_, t.known_, t.done_); }
void ioClass(Ar& a, OpenRisc& o) { a.tag("OpenRisc"); a(o.r_, o.spr_, o.pc_, o.ppc_, o.sr_, o.branch_, o.instructions_, o.exceptions_, o.running_); }
void ioClass(Ar& a, PmBootReason& p) { a.tag("PmBoot"); a(p.cause_); }
void ioClass(Ar& a, PmPowerGood& p) { a.tag("PmPowerGood"); a(p.externalGood_, p.transitions); }
void ioClass(Ar& a, SarAdc& s) { a.tag("Sar"); a(s.inputs_, s.result_, s.running_, s.due_, s.period_, s.conversions_); }
void ioClass(Ar& a, ScartGroundedInput& s) { a.tag("Scart"); a(s.grounded_, s.initialized_, s.configured_, s.reads_); }
void ioClass(Ar& a, VifZeroInput& v) { a.tag("VifZero"); a(v.enabled_, v.armed_, v.settled_, v.settling_, v.due_, v.settlements_, v.config_); }
void ioClass(Ar& a, PqSram& p) {
  a.tag("PqSram");
  a(p.idleKnown_, p.data, p.written, p.submissions, p.commits);
  bool has = p.pending_.has_value();
  a(has);
  if (a.loading()) { if (has) p.pending_ = PqSram::Transfer{}; else p.pending_.reset(); }
  if (has) a(p.pending_->plane, p.pending_->address, p.pending_->bytes, p.pending_->ready);
}
void ioClass(Ar& a, IrDma& d) { a.tag("IrDma"); a(d.base_, d.capacity_, d.index_, d.starts_, d.captures_, d.capturedSinceStart_); }
void ioClass(Ar& a, PmIrFifo& f) {
  a.tag("PmIrFifo");
  uint64_t n = a.count(f.entries_.size());
  if (a.loading()) { f.entries_.clear(); f.entries_.resize(size_t(n)); }
  for (auto& e : f.entries_) a(e.key, e.repeat);
  a(f.known_, f.rptLatch_, f.lastRepeat_, f.clears_, f.pops_, f.received_);
}
void ioClass(Ar& a, UtmiCalibration& u) { a.tag("Utmi"); a(u.clockReset_, u.clockEnabled_, u.started_, u.released_, u.start_, u.ready_); }
void ioClass(Ar& a, Rtc& r) { a.tag("Rtc"); a(r.lastTick_, r.oscillatorFraction_, r.dividerFraction_, r.counter_, r.snapshot_, r.control_, r.snapshotValid_, r.error); }
void ioClass(Ar& a, VdMcuProgram& p) { a.tag("VdProgram"); a(p.hold_, p.control_, p.low_, p.high_, p.execution_, p.cursor_, p.code_, p.valid_, p.writes, p.reads, p.startRequests, p.interruptRequests, p.interruptPending); }
void ioClass(Ar& a, AvdMcuProgram& p) { a.tag("AvdProgram"); a(p.hold_, p.mode_, p.low_, p.high_, p.cursor_, p.code_, p.valid_, p.writes, p.reads, p.startRequests); }
void ioClass(Ar& a, AvdMcuBus& b) {
  a.tag("AvdBus");
  a(b.ram_, b.deferred_, b.deferStartup_, b.ramReads, b.ramWrites, b.sharedReads, b.sharedWrites, b.configurationWrites, b.symbolicTableConfiguration,
    b.eventAckClearOnly, b.invalidDataPublication, b.eventAcks);
}
void ioClass(Ar& a, AudioIdma& d) {
  a.tag("AudioIdma");
  a(d.words_, d.staged_, d.readback_, d.writeAddress_, d.readAddress_, d.phase_, d.readbackKnown_, d.writeCompleted_, d.readCompleted_, d.wordWrites, d.wordReads);
}
void ioClass(Ar& a, UnknownObservations& u) { a.tag("UnknownObs"); a(u.registers_, u.timingRegisters_, u.clockOrigins_, u.bytes_, u.spiCacheAddress_, u.spiCacheArmed_); if (a.loading()) u.restored(); }
void ioClass(Ar& a, PreservedBits& p) {
  a.tag("PreservedBits");
  for (auto& reg : p.registers_) for (auto& b : reg) a(b.byte, b.bit, b.inverted);
  uint64_t n = a.count(p.memory_.size());
  if (a.saving()) {
    for (auto& [k, v] : p.memory_) { uint32_t kk = k; a(kk); for (auto& b : v) a(b.byte, b.bit, b.inverted); }
  } else {
    p.memory_.clear();
    for (uint64_t i = 0; i < n; ++i) {
      uint32_t k = 0; std::array<PreservedBits::Bit, 8> v{};
      a(k);
      for (auto& b : v) a(b.byte, b.bit, b.inverted);
      p.memory_.emplace(k, v);
    }
  }
  a(p.started_);
  if (a.loading()) p.restored();
}
void ioClass(Ar& a, AudioNoAck& n) { a.tag("AudioNoAck"); a(n.pending_, n.branch_, n.samples, n.comparisons, n.timeouts, n.returns); }
void ioClass(Ar& a, PmActivityScenario& s) { a.tag("PmActivity"); a(s.stage_, s.samples, s.predicates, s.branches); }
void ioClass(Ar& a, Flash& f) { a.tag("Flash"); a(f.sectors_, f.wel_, f.protected_); }
void ioClass(Ar& a, Pm51& p) {
  a.tag("Pm51");
  a(p.ram_, p.written_, p.visited_, p.romControl_);
  uint64_t n = a.count(p.deferredConfigurations_.size());
  if (a.loading()) { p.deferredConfigurations_.clear(); p.deferredConfigurations_.resize(size_t(n)); }
  for (auto& d : p.deferredConfigurations_) a(d.pc, d.address, d.payload);
  a(p.tick_, p.started_, p.suspended_, p.boundary_);
  uint64_t hi = a.count(p.instructionHistory_.size());
  if (a.loading()) { p.instructionHistory_.clear(); p.instructionHistory_.resize(size_t(hi)); }
  for (auto& i : p.instructionHistory_) a(i.tick, i.count, i.pc, i.dptr, i.origin, i.opcode, i.accumulator, i.known, i.stack, i.status, i.page, i.dispatched);
  uint64_t hb = a.count(p.busHistory_.size());
  if (a.loading()) { p.busHistory_.clear(); p.busHistory_.resize(size_t(hb)); }
  for (auto& b : p.busHistory_) a(b.tick, b.pc, b.address, b.page, b.byte, b.write, b.accepted);
  a(p.divider, p.budget, p.ramReads, p.ramWrites, p.riuReads, p.riuWrites, p.markerWrites, p.controlWrites, p.firstVisits, p.captureHistory,
    p.probeDeferredStartupConfigurations);
  p.cpu_.snapshot(a, [&](VdMcuCpu&) { p.rebind(p.code_); });
}

// ------------------------------------------------------------------------------------------------------------------------- RIU
void ioClass(Ar& a, Riu& r) {
  a.tag("Riu");
  a(r.gopUpdate_, r.gopActive_, r.exploratoryFetchRevisionByte_, r.vdMcuProgram_, r.avdMcuProgram_);
  r.vdMcuCpu_.snapshot(a, [&](VdMcuCpu& c) { c.rebindProgram(r.vdMcuProgram_); });
  r.avdMcuCpu_.snapshot(a, [&](VdMcuCpu& c) { c.rebindProgram(r.avdMcuProgram_); });
  a(r.avdMcuBus_, r.avdTick_, r.audioIdma_[0], r.audioIdma_[1], r.avdMcuSuspended_, r.avdMcuBoundaryReason_, r.vdFiqMask_, r.vdVersionBytes_, r.vdPrivateRam_,
    r.vdTick_, r.vdMailboxReads_, r.vdMailboxWrites_, r.vdVisited_, r.vdLastMailboxReads_, r.vdMcuSuspended_, r.vdMcuBoundaryReason_, r.bytes_,
    r.bdmaComplete_, r.bdmaPending_, r.bdmaCopyBytes_, r.bdmaCopyDestination_, r.pendingRmw_, r.consumedRead_, r.preservedUnknownWrite_, r.opaqueRead_,
    r.uartEnabled_, r.gpio_, r.rtc_[0], r.rtc_[1], r.utmi_[0], r.utmi_[1], r.spi_, r.semaphore_, r.gamma_, r.scaler_, r.ge_, r.palette_, r.tsp_,
    r.tspReadTarget_, r.tspIndirectReads_, r.tspIndirectWrites_, r.tspDownload_, r.tspDownloadSource_, r.tspCpu_, r.exploratoryTspCpu_, r.pmBoot_,
    r.pmPowerReasonWritten_);
  if (a.loading()) r.pm51_.setSnapshotCode(r.pmCode_);
  a(r.pm51_, r.pmPowerGood_, r.exploratoryPm51_, r.sar_, r.exploratorySarFullscale_, r.exploratoryScartIdGrounded_, r.scartIdSamples_, r.scart_,
    r.analogDisconnected_, r.exploratoryAvdNoHsync_, r.avdHsyncReads_, r.exploratoryUsbNoDevice_, r.diagnosticUsbUnpoweredDplus_, r.diagnosticVifCrLock_,
    r.guardedStretchDetail_, r.exploratoryAvdHtotal_, r.exploratoryAvdNoise_);
  if (r.exploratoryAtvSignal_) a(r.atvEventByte_, r.atvFieldIndex_, r.atvFieldSteps_, r.atvHTotal_, r.atvFieldLines_[0], r.atvFieldLines_[1]);
  uint64_t nh = a.count(r.hypothesisBits_.size());
  auto configured = r.hypothesisBits_;   // hypotheses requested by THIS process (superset load adds some the snapshot never had)
  if (a.loading()) { r.hypothesisBits_.clear(); r.hypothesisBits_.resize(size_t(nh)); }
  for (auto& h : r.hypothesisBits_) a(h.address, h.mask, h.value, h.reads, h.scBank);
  if (a.loading())
    for (auto& c : configured) {
      bool have = false;
      for (auto& h : r.hypothesisBits_) have = have || (h.address == c.address && h.scBank == c.scBank && h.mask == c.mask);
      if (!have) { c.reads = 0; r.hypothesisBits_.push_back(c); }
      else for (auto& h : r.hypothesisBits_) if (h.address == c.address && h.scBank == c.scBank && h.mask == c.mask) { h.rmw = c.rmw; h.init = c.init; h.ack = c.ack; }
    }
  a(r.exploratoryAfecIndirect_, r.afecIndirectValue_, r.afecIndirectReads_, r.afecIndirectWrites_, r.avdNoiseReads_, r.avdHtotalReads_, r.vifCrLockReads_,
    r.exploratoryAvdNoSyncEvents_, r.diagnosticAvd11dbBit7_, r.exploratoryVifRevision_, r.vifRevisionReads_, r.vifZeroInput_, r.vifZeroInputReads_,
    r.pqSram_, r.irDma_, r.exploratoryIrDmaReset_, r.exploratoryIrCapturePresent_, r.irCaptureStatusReads_, r.pmIrFifo_, r.candidatePmIrFifo_,
    r.pmIrStatusReads_, r.exploratoryGopResetWindows_, r.gopResetHeld_, r.gopResetPulses_, r.orFirstVisits_, r.orBudget_, r.orOriginalWords_,
    r.orModifiedWords_, r.orDivider_, r.orVisited_, r.orVisitedQmem_, r.tspCpuTick_, r.tspCpuReads_, r.tspCpuWrites_);
  for (auto& m : r.tspBurstMapping_) a(m.base, m.mask, m.known);
  a(r.tspBurstMappingActive_, r.lastOutputSync_, r.outputSyncReads_, r.outputSyncTransitions_);
  bool hasComb = r.combRestore_.has_value();
  a(hasComb);
  if (a.loading()) { if (hasComb) r.combRestore_ = Riu::CombRestore{}; else r.combRestore_.reset(); }
  if (hasComb) { auto& c = *r.combRestore_; a(c.address, c.bank, c.low, c.unknown, c.written, c.pcWrite, c.pcRestore, c.lowUnknown); }
  a(r.combRestoreCount_, r.timingSnapshot_, r.timingSnapshotPending_);
  for (auto& v : r.miuSequence_) a(v);
  for (auto& v : r.miuReady_) a(v);
  for (auto& v : r.miuArmed_) a(v);
  a(r.bistReady_, r.bistResult_, r.bistPending_, r.uartReady_);
  // public counters and configuration flags (configuration must equal the constructor's; the options fingerprint enforces that)
  a(r.candidateVdMcuProgram, r.candidateAvdMcuProgram, r.candidateAudioIdma, r.exploratoryAvdMcuStartup, r.exploratoryAvdSuspendedBoundary,
    r.exploratoryAvdSharedRun, r.diagnosticAvdDeferredStartup, r.diagnosticAvdSymbolicCodeTables, r.exploratoryAvdHostResetEmpty, r.avdHostRequestLatchResets,
    r.avdDispatchDivider, r.avdInstructionBudget, r.exploratoryVdMcuStartup, r.deferVdSfr8eWrite, r.exploratoryVdSuspendedBoundary, r.deferVdResetKeyWrites,
    r.exploratoryVdSharedRun, r.vdDispatchDivider, r.vdInstructionBudget, r.candidateTsp, r.exploratoryCiDetectHigh, r.ciDetectReads, r.candidateGopPalette,
    r.candidateGe, r.candidateGeRectFill, r.candidateGeBitBlt, r.geBitBlts, r.geBitBltPixels, r.gopLog, r.geReg0Writes, r.geRectangles, r.geRasterPixels,
    r.exploratoryGeUniformStretch, r.exploratoryGeNearestStretch, r.geNearestStretches, r.geUniformStretches, r.geUniformStretchPixels,
    r.exploratoryGeI2Glyph, r.exploratoryGeBlendCopy, r.exploratoryGeFillIgnoredReg00, r.exploratoryGeGradientFill, r.geGradientFills, r.exploratoryGeI8Fill,
    r.geI8Fills, r.geI8FillPixels, r.copyRefusal_, r.exploratoryGeCopySnapshot, r.exploratoryGeHeight5Full, r.geBlendCopies, r.geBlendCopyPixels,
    r.geBlendCopyLog, r.geI2Glyphs, r.geI2GlyphPixels, r.candidateSemaphore, r.exploratoryCombRestore, r.exploratoryDlcTimeout, r.exploratoryDisplayClock,
    r.exploratoryGopVsyncLatch, r.symbolicInit, r.candidateBdma, r.gpioI2c, r.candidateMiu, r.virtualRtc, r.candidateUtmi, r.candidateSpi, r.candidateChipId,
    r.candidateGamma, r.stopReason, r.uart);
}

// ---------------------------------------------------------------------------------------------------------------- machine state
void ioClass(Ar& a, Msd309Machine& m) {
  a.tag("Machine");
  a(m.coproLoadInspections_, m.coproLoadedSha_, m.suppressedOrQmemTrace_, m.suppressedOrIdlePolls_, m.lastOrIdlePollPc_, m.suppressedOrStoredReads_,
    m.lastOrStoredRead_, m.coverage_, m.pcHits_, m.polls_, m.writeEpoch_, m.maxPoll_, m.history_, m.recentInstructions_, m.recentMmio_, m.unknownObservations_,
    m.preservedBits_, m.audioNoAck_, m.pmActivityScenario_, m.deferredReads_, m.timerEntries_, m.gopEvents_, m.geEvents_, m.geBlockedEvents_,
    m.gopInitializerEntered_, m.gopInitializerEntries_, m.gopWindowProgramEntries_, m.gopWrite32Entries_, m.fetchPatchEnabled_, m.fetchPatchDisabled_,
    m.osdProbeEntries_);
  a(m.zuiFlow_.entries, m.zuiFlow_.snapshots, m.zuiFlow_.signatureFailures, m.zuiFlow_.dropped);
  a(m.scalerInitializerEntries_, m.scalerInitializerReturns_, m.dlcInitializerReturns_, m.spiJedecRead_, m.spiDescriptorSelected_, m.spiJedec_,
    m.spiDescriptor_, m.dispatches_, m.mmio_, m.flashReads_, m.dramWrites_, m.stopped_, m.joinResume_, m.restoreBranchSeen_, m.joinResumePc_, m.htotalJoins_,
    m.blockingPc_, m.blockingAddress_, m.reason_, m.stage2Reached_, m.decompressReached_, m.runtimeReached_, m.guestDecompressedSha_, m.preparingPrefix_,
    m.opaquePolicyReads_, m.opaquePolicyRules_, m.watchHits_, m.irLog_, m.irWaiting_, m.historyChecksum_, m.historyFrames_, m.historyIndex_);
  uint64_t nt = a.count(m.tlbInstalls_.size());
  if (a.loading()) { m.tlbInstalls_.clear(); m.tlbInstalls_.resize(size_t(nt)); }
  for (auto& t : m.tlbInstalls_) a(t.index, t.pageMask, t.entryHi, t.entryLo0, t.entryLo1);
  a(m.cp0Shadow_, m.tlbwrSeen_);
}

// ------------------------------------------------------------------------------------------------------------------ file format
namespace {
constexpr char kMagic[8] = {'M', 'A', 'N', 'T', 'A', 'S', 'N', 'P'};
constexpr uint32_t kVersion = 1;
using Section = Msd309Machine::SnapSection;
std::vector<uint8_t> assemble(const std::vector<Section>& sections) {
  std::vector<uint8_t> out;
  Ar a(out);
  a.raw(const_cast<char*>(kMagic), 8);
  uint32_t v = kVersion; a(v);
  uint32_t n = uint32_t(sections.size()); a(n);
  for (auto& s : sections) {
    char name[16]{};
    std::memcpy(name, s.name.data(), std::min<size_t>(15, s.name.size()));
    a.raw(name, 16);
    uint64_t len = s.bytes.size(); a(len);
    auto digest = sha256(s.bytes);
    char hex[64]; std::memcpy(hex, digest.data(), 64);
    a.raw(hex, 64);
    a.raw(const_cast<uint8_t*>(s.bytes.data()), s.bytes.size());
  }
  auto total = sha256(out);
  char hex[64]; std::memcpy(hex, total.data(), 64);
  a.raw(hex, 64);
  return out;
}
std::vector<Section> disassemble(const std::vector<uint8_t>& file) {
  if (file.size() < 8 + 4 + 4 + 64) throw SnapshotError("snapshot file truncated");
  auto bodySize = file.size() - 64;
  std::string expected(reinterpret_cast<const char*>(file.data()) + bodySize, 64);
  if (sha256(std::span<const uint8_t>(file.data(), bodySize)) != expected) throw SnapshotError("snapshot overall checksum mismatch (truncated or corrupted file)");
  Ar a(file.data(), bodySize);
  char magic[8]; a.raw(magic, 8);
  if (std::memcmp(magic, kMagic, 8)) throw SnapshotError("not a Manta snapshot file");
  uint32_t version = 0, n = 0; a(version); a(n);
  if (version != kVersion) throw SnapshotError("unsupported snapshot schema version " + std::to_string(version));
  if (n > 64) throw SnapshotError("snapshot section count implausible");
  std::vector<Section> out;
  for (uint32_t i = 0; i < n; ++i) {
    char name[17]{}; a.raw(name, 16);
    uint64_t len = 0; a(len);
    char hex[65]{}; a.raw(hex, 64);
    if (len > a.remaining()) throw SnapshotError("snapshot section length exceeds file");
    Section s; s.name = name; s.bytes.resize(size_t(len));
    a.raw(s.bytes.data(), size_t(len));
    if (sha256(s.bytes) != std::string(hex, 64)) throw SnapshotError("snapshot section checksum mismatch: " + s.name);
    out.push_back(std::move(s));
  }
  if (a.remaining()) throw SnapshotError("snapshot has trailing data");
  return out;
}
const Section& find(const std::vector<Section>& s, const char* name) {
  for (auto& x : s) if (x.name == name) return x;
  throw SnapshotError(std::string("snapshot is missing required section ") + name);
}
}  // namespace

bool Msd309Machine::snapshotBoundaryOk(uint32_t pc) {
  if (stopped_ || joinResume_ || preparingPrefix_ || replayingTlb_ || tlbwrSeen_) return false;
  if (trace_.is_open() || replay_.is_open()) return false;
  // GE closures cannot be serialized: wait for the original queue to drain by itself (never forced).
  if (!riu_.geQueueIdle()) return false;
  // A branch/jump immediately before this instruction would leave this one in a delay slot: the resume PC alone cannot express that.
  if (!historyEmpty()) {
    uint32_t prev = lastHistoryPc() & 0x1fffffffu, w = 0;
    if (uint64_t(prev) + 4 <= dramBacking_.size() && !dramBacking_.empty()) std::memcpy(&w, dramBacking_.data() + prev, 4); else return false;
    unsigned op = w >> 26, fn = w & 63;
    bool branch = op == 1 || (op >= 2 && op <= 7) || (op >= 20 && op <= 23) || (op == 16 && ((w >> 21) & 31) == 8) || (op == 17 && ((w >> 21) & 31) == 8) ||
                  (op == 0 && (fn == 8 || fn == 9));
    if (branch) return false;
  }
  // A pending CP0 timer interrupt (Cause.TI / IP7) lives partly in QEMU's CPUState, which a context restore does not carry.
  uint32_t cause = cpu_.reg(UC_MIPS_REG_CP0_CAUSE);
  if (cause & ((1u << 30) | (1u << 15))) return false;
  (void)pc;
  return true;
}

std::vector<Msd309Machine::SnapSection> Msd309Machine::captureSections(uint32_t pc) {
  flushStats();
  flushTicks();
  std::vector<Section> sections;
  {
    Section s{"meta", {}};
    Ar a(s.bytes);
    a.tag("meta");
    std::string fw = flash_.stockDigest(), fp = options_.stateFingerprint, canon = options_.stateCanonical;
    uint32_t cpi = virtualTime_.cpi();
    uint64_t cycles = virtualTime_.cycles(dispatches_), ramSize = dramBacking_.size();
    a(fw, fp, pc, dispatches_, cycles, cpi, ramSize, canon);
    sections.push_back(std::move(s));
  }
  {
    Section s{"mips", {}};
    Ar a(s.bytes);
    a.tag("mips");
    uc_context* ctx = nullptr;
    check(uc_context_alloc(cpu_.handle, &ctx));
    check(uc_context_save(cpu_.handle, ctx));
    size_t total = uc_context_size(cpu_.handle), payload = *reinterpret_cast<size_t*>(ctx);
    std::vector<uint8_t> blob(payload);
    std::memcpy(blob.data(), reinterpret_cast<uint8_t*>(ctx) + (total - payload), payload);
    uc_context_free(ctx);
    a(blob);
    sections.push_back(std::move(s));
  }
  {
    Section s{"dram", {}};
    Ar a(s.bytes);
    a.tag("dram");
    constexpr size_t kPage = 4096;
    std::vector<uint32_t> pages;
    for (size_t off = 0; off + kPage <= dramBacking_.size(); off += kPage) {
      const uint8_t* p = dramBacking_.data() + off;
      bool nonzero = false;
      for (size_t i = 0; i < kPage; i += 8) { uint64_t q; std::memcpy(&q, p + i, 8); if (q) { nonzero = true; break; } }
      if (nonzero) pages.push_back(uint32_t(off / kPage));
    }
    // Non-zero pages are stored once per distinct content (memory-test patterns fill most of RAM with one identical page).
    std::unordered_map<std::string, uint32_t> unique;
    std::vector<std::string> uniquePages;
    std::vector<uint32_t> refs;
    for (auto idx : pages) {
      std::string page(reinterpret_cast<const char*>(dramBacking_.data()) + size_t(idx) * kPage, kPage);
      auto it = unique.find(page);
      if (it == unique.end()) { it = unique.emplace(page, uint32_t(uniquePages.size())).first; uniquePages.push_back(page); }
      refs.push_back(it->second);
    }
    uint64_t total = dramBacking_.size(); a(total);
    a(pages, refs);
    uint64_t nu = uniquePages.size(); a(nu);
    for (auto& page : uniquePages) a.raw(page.data(), kPage);
    sections.push_back(std::move(s));
  }
  { Section s{"flash", {}}; Ar a(s.bytes); ioClass(a, flash_); sections.push_back(std::move(s)); }
  { Section s{"riu", {}}; Ar a(s.bytes); ioClass(a, riu_); sections.push_back(std::move(s)); }
  { Section s{"machine", {}}; Ar a(s.bytes); ioClass(a, *this); sections.push_back(std::move(s)); }
  if (riu_.usbHost.enabled()) { Section s{"usb", {}}; Ar a(s.bytes); riu_.usbHost.serialize(a); sections.push_back(std::move(s)); }   // EXPLORATORY pendrive state
  if (options_.exploratoryCopro && copro_.started) {   // EXPLORATORY coprocessor model state
    Section s{"copro", {}}; Ar a(s.bytes);
    copro_.serialize(a);
    a(riu_.fiqPending, riu_.fiqRaises, riu_.fiqStatusReads, riu_.fiqStatusWrites, riu_.fiqLineChanges, irqLinesApplied_);
    sections.push_back(std::move(s));
  }
  return sections;
}
void Msd309Machine::writeSnapshot(uint32_t pc) {
  auto sections = captureSections(pc);
  auto file = assemble(sections);
  auto tmp = options_.saveState; tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
    f.flush();
    if (!f) { std::error_code ec; std::filesystem::remove(tmp, ec); throw SnapshotError("cannot write snapshot file"); }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, options_.saveState, ec);
  if (ec && std::filesystem::is_regular_file(options_.saveState)) { std::error_code ec2; std::filesystem::remove(options_.saveState, ec2); ec.clear(); std::filesystem::rename(tmp, options_.saveState, ec); }
  if (ec) { std::error_code ec3; std::filesystem::remove(tmp, ec3); }
  if (ec) throw SnapshotError("cannot commit snapshot file: " + ec.message());
  event("save_state_written", pc, 0, uint32_t(file.size()), "full-machine snapshot written at a safe instruction boundary (dispatch " + std::to_string(dispatches_) + ")");
}

void Msd309Machine::restoreSnapshot(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw SnapshotError("cannot open snapshot " + path.string());
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto sections = disassemble(file);
  uint32_t pc = 0, cpi = 0;
  uint64_t cycles = 0, ramSize = 0, dispatches = 0;
  std::vector<std::string> extraTokens;   // option tokens present now but not in the snapshot (superset load)
  {
    auto& s = find(sections, "meta");
    Ar a(s.bytes.data(), s.bytes.size());
    a.tag("meta");
    std::string fw, fp, canon;
    a(fw, fp, pc, dispatches, cycles, cpi, ramSize);
    if (a.remaining()) a(canon);   // option lines (absent in the first snapshots of schema 1)
    if (fw != flash_.stockDigest()) throw SnapshotError("snapshot was taken with a different firmware image");
    if (fp != options_.stateFingerprint) {
      // Optionally accept additional options: every option of the snapshot must still be present (as a multiset of lines).
      auto split = [](const std::string& text) { std::vector<std::string> v; std::string cur; for (char c : text) { if (c == char(10)) { v.push_back(cur); cur.clear(); } else cur.push_back(c); } return v; };
      auto oldLines = split(canon), nowLines = split(options_.stateCanonical);
      bool subset = options_.loadAllowExtraOptions && !canon.empty();
      std::vector<std::string> remaining = nowLines;
      if (subset) for (auto& l : oldLines) { auto it = std::find(remaining.begin(), remaining.end(), l); if (it == remaining.end()) { subset = false; break; } remaining.erase(it); }
      if (!subset) throw SnapshotError("snapshot was taken under a different run configuration (diagnostic assumptions/options differ)");
      for (auto& l : remaining) snapshotOptionDelta_ += l + " ";
      extraTokens = remaining;
    }
    if (cpi != virtualTime_.cpi() || ramSize != dramBacking_.size()) throw SnapshotError("snapshot hardware configuration (CPI/RAM size) differs");
  }
  Ar mach(find(sections, "machine").bytes.data(), find(sections, "machine").bytes.size());
  // machine section first: it carries the TLB install log needed before the CPU context is restored.
  ioClass(mach, *this);
  if (!options_.gopFrameHistory.empty()) {   // host-side diagnostic only: a run's frame history starts empty (the state's own history would hide an unchanged first frame and use up the 128-frame cap)
    historyChecksum_ = 0; historyFrames_ = 0; historyIndex_.clear();
  }
  if (tlbwrSeen_) throw SnapshotError("snapshot taken after a TLBWR: random TLB replacement cannot be replayed");
  // 1. replay the guest's TLBWI installs (hooks gated), then 2. restore the CPU context over the resulting TLB.
  if (!tlbInstalls_.empty()) {
    replayingTlb_ = true;
    std::vector<uint32_t> code;
    auto movi = [&](unsigned rd, uint32_t value) {
      code.push_back(0x3c080000u | (value >> 16));           // lui $t0, hi
      code.push_back(0x35080000u | (value & 0xffff));        // ori $t0, $t0, lo
      code.push_back(0x40880000u | (rd << 11));              // mtc0 $t0, $rd
    };
    for (auto& t : tlbInstalls_) { movi(5, t.pageMask); movi(10, t.entryHi); movi(2, t.entryLo0); movi(3, t.entryLo1); movi(0, t.index); code.push_back(0x42000002u); code.push_back(0); }
    constexpr uint32_t base = 0x00001000;
    cpu_.write(base, code.data(), code.size() * 4);
    auto e = cpu_.run(0x80000000u | base, code.size());
    replayingTlb_ = false;
    if (e != UC_ERR_OK) throw SnapshotError(std::string("TLB replay failed: ") + uc_strerror(e));
  }
  {
    auto& s = find(sections, "mips");
    Ar a(s.bytes.data(), s.bytes.size());
    a.tag("mips");
    std::vector<uint8_t> blob; a(blob);
    uc_context* ctx = nullptr;
    check(uc_context_alloc(cpu_.handle, &ctx));
    size_t total = uc_context_size(cpu_.handle), payload = *reinterpret_cast<size_t*>(ctx);
    if (blob.size() != payload) { uc_context_free(ctx); throw SnapshotError("CPU context size differs from this Unicorn build"); }
    std::memcpy(reinterpret_cast<uint8_t*>(ctx) + (total - payload), blob.data(), payload);
    auto err = uc_context_restore(cpu_.handle, ctx);
    ++cpu_.writeGen;
    pendingTicks_ = 0; tickSynced_ = false;
    uc_context_free(ctx);
    check(err);
  }
  {
    auto& s = find(sections, "dram");
    Ar a(s.bytes.data(), s.bytes.size());
    a.tag("dram");
    uint64_t total = 0; a(total);
    if (total != dramBacking_.size()) throw SnapshotError("snapshot RAM size differs");
    std::vector<uint32_t> pages, refs; a(pages, refs);
    constexpr size_t kPage = 4096;
    if (pages.size() != refs.size()) throw SnapshotError("snapshot RAM page table inconsistent");
    uint64_t nu = 0; a(nu);
    if (nu > a.remaining() / kPage) throw SnapshotError("snapshot RAM page store truncated");
    std::vector<std::vector<uint8_t>> uniquePages;
    uniquePages.resize(size_t(nu));
    for (auto& page : uniquePages) page.resize(kPage);
    for (auto& page : uniquePages) a.raw(page.data(), kPage);
    std::fill(dramBacking_.begin(), dramBacking_.end(), 0);
    for (size_t i = 0; i < pages.size(); ++i) {
      if (size_t(pages[i]) * kPage + kPage > dramBacking_.size() || refs[i] >= uniquePages.size()) throw SnapshotError("snapshot page index outside RAM");
      std::memcpy(dramBacking_.data() + size_t(pages[i]) * kPage, uniquePages[refs[i]].data(), kPage);
    }
  }
  { auto& s = find(sections, "flash"); Ar a(s.bytes.data(), s.bytes.size()); ioClass(a, flash_); }
  { auto& s = find(sections, "riu"); Ar a(s.bytes.data(), s.bytes.size()); ioClass(a, riu_); }
  {
    bool haveUsb = false;
    for (auto& s : sections) if (s.name == "usb") { haveUsb = true; if (!riu_.usbHost.enabled()) throw SnapshotError("snapshot holds USB pendrive state but --exploratory-usb-pendrive is absent"); Ar a(s.bytes.data(), s.bytes.size()); riu_.usbHost.serialize(a); }
    (void)haveUsb;   // absent section + attached option: fresh device (superset load of the option)
  }
  {
    bool haveCopro = false;
    for (auto& s : sections) if (s.name == "copro") {
      haveCopro = true;
      if (!options_.exploratoryCopro) throw SnapshotError("snapshot holds coprocessor state but --exploratory-copro is absent");
      Ar a(s.bytes.data(), s.bytes.size());
      copro_.serialize(a);
      a(riu_.fiqPending, riu_.fiqRaises, riu_.fiqStatusReads, riu_.fiqStatusWrites, riu_.fiqLineChanges, irqLinesApplied_);
      attachCopro();
    }
    (void)haveCopro;
  }
  riu_.reapplyInitHypotheses();
  riu_.exploratoryGeLine = options_.exploratoryGeLine;
  riu_.usbDeviceAttachedPort0 = !options_.exploratoryUsbPendrive.empty();   // not part of the snapshot format: always taken from the current options
  // Superset load: options added after the snapshot are either runtime-read (accepted), applied to the restored devices here, or REFUSED.
  // Silently ignoring an added option whose state lives in the serialized device configuration would mislabel the run.
  for (size_t i = 0; i < extraTokens.size(); ++i) {
    const auto& t = extraTokens[i];
    if (t.rfind("--", 0) != 0) continue;   // a value token, consumed with its option below
    if (t == "--exploratory-provenance-extensions" || t == "--trace-instructions" || t == "--exploratory-ge-line") continue;
    // Coprocessor / JPD / FIQ / YUV-stretch / zero-unknown diagnostics: all read at run time, none has state in the snapshot (the coprocessor model refuses a snapshot taken
    // after the guest released the core).
    if (t == "--exploratory-copro" || t == "--exploratory-copro-zero-unknown-reads" || t == "--exploratory-copro-jpd" || t == "--exploratory-ge-yuv-stretch" || t == "--exploratory-bdma-crc-miu" || t == "--exploratory-bdma-mem-copy" || t == "--exploratory-atv-picture" || t == "--native-cpu" ||
        t == "--diagnostic-riu-zero-unknown-reads") continue;
    if (t == "--exploratory-copro-mips-irq" || t == "--exploratory-copro-jpd-dump" || t == "--exploratory-copro-capture" || t == "--exploratory-copro-watch" ||
        t == "--exploratory-copro-dump-start") { ++i; continue; }
    if (t == "--exploratory-copro-seq") { i += 2; continue; }
    if (t == "--exploratory-input-timing" || t == "--exploratory-hdmi-signal" || t == "--exploratory-input-picture") { ++i; continue; }   // generated input-signal timing: read at run time, no snapshot state
    if (t == "--diagnostic-riu-write-log" || t == "--diagnostic-riu-peek" || t == "--gop-history-interval-log2") { ++i; continue; }
    if (t == "--exploratory-mvop-plane") continue;
    if (t == "--diagnostic-usb-save-image") { ++i; continue; }
    if (t == "--diagnostic-dram-dump") { i += 3; continue; }
    if (t == "--exploratory-usb-pendrive") { ++i; continue; }   // read at run time (device model is created after the restore)   // read from the options at run time
    if (t == "--exploratory-mmio-bits") { ++i; continue; }                                       // merged into the restored hypothesis list
    if (t == "--diagnostic-vif-cr-lock" && i + 1 < extraTokens.size()) {
      riu_.configureDiagnosticVifCrLock(unsigned(std::stoul(extraTokens[i + 1])));
      ++i; continue;
    }
    throw SnapshotError("option " + t + " cannot be added to a snapshot taken without it (its state is part of the snapshot); recreate the checkpoint with it");
  }
  if (mach.remaining()) throw SnapshotError("machine section has trailing data");
  // CPU-window mirrors of flash (guest SPI writes update the overlay): refresh from the restored overlay.
  {
    auto aperture = riu_.flashRead(0, 0x400000);
    cpu_.write(0x1fc00000, aperture.data(), aperture.size());
    if (options_.candidateSpi) { auto xiu = riu_.flashRead(0, 0x800000); cpu_.write(0x14000000, xiu.data(), xiu.size()); }
  }
  check(uc_ctl_flush_tb(cpu_.handle));
  if (dispatches_ != dispatches) throw SnapshotError("snapshot dispatch counter inconsistent");
  importStats();
  resumePc_ = pc;
  resumed_ = true;
  if (options_.exploratoryCpiAfterLoad) virtualTime_.switchCpi(uint32_t(options_.exploratoryCpiAfterLoad), dispatches_);   // EXPLORATORY: time runs slower per instruction from the restored state on
  if (options_.exploratoryCopro && copro_.started && irqLinesApplied_) cpu_.setIrqLines(irqLinesApplied_);
}
}  // namespace manta
