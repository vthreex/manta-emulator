#pragma once
#include "snapshot/archive.hpp"
#include "peripherals/vd_mcu_program.hpp"
#include "peripherals/avd_mcu_program.hpp"
#include <functional>
#include <memory>
#include <string>
#include <stdexcept>
#include <optional>
#include <span>
namespace manta {
struct VdByte { uint8_t value=0xa5,known=0;uint16_t origin=0; };
struct VdHardwareBoundary:std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Bounded auxiliary startup experiment, not a complete MStar MCU hardware
// model. Original code storage is shared with the guest programming port.
class VdMcuCpu {
  friend void ioClass(snap::Ar&, VdMcuCpu&);
  struct Impl;
  std::unique_ptr<Impl> impl_;
  void resetProgramStorage(std::span<const uint8_t> code,
    std::function<bool(uint16_t)> byteKnown,uint64_t writes);
public:
  VdMcuCpu();
  ~VdMcuCpu();
  VdMcuCpu(const VdMcuCpu&)=delete;
  std::function<void(uint16_t,uint8_t,const std::string&)> instruction;
  std::function<void(uint16_t,bool,uint8_t)> external;
  std::function<void(uint8_t,bool,uint8_t)> specialRegister;
  std::function<void(int)> coreDiagnostic;
  bool deferSfr8eWrite=false;
  std::function<void(uint8_t)> deferredSfr8e;
  bool deferResetKeyWrites=false;
  std::function<void(uint16_t,uint8_t)> deferredResetKey;
  std::function<std::optional<VdByte>(uint16_t)> busRead;
  std::function<bool(uint16_t,VdByte)> busWrite;
  // Optional peripheral callback; false retains the unsupported-SFR boundary.
  // It supplies no SFR read value and never marks a deferred effect known.
  std::function<bool(uint8_t,VdByte)> unsupportedSfrWrite;
  bool standardInterrupts=false;
  bool candidateR8051Uart=false; // configuration only; no guessed RX/TX completion
  // Conservative data-only ROM lookups, with no unknown XDATA address,
  // instruction target, branch outcome or device value supplied.
  bool symbolicCodeTables=false;
  void resetProgram(VdMcuProgram& program);
  void resetProgram(AvdMcuProgram& program);
  // Isolated standard-reset experiment; not a board SPI fetch mapping.
  void resetPmCandidate(std::span<const uint8_t> code,std::function<bool(uint16_t)> sourceValid={});
  void step();
  void requestInt0();
  bool running()const;
  uint64_t totalInstructions()const;
  uint64_t interruptEntries()const;
  void startup(VdMcuProgram& program,uint64_t cycleBudget=10000);
  uint64_t instructions()const;
  uint64_t cycles()const;
  uint16_t pc()const;
  uint8_t stack()const;
  uint8_t accumulator()const;
  uint8_t status()const;
  uint8_t port2()const;
  uint16_t dataPointer()const;
  uint8_t accumulatorKnown()const;
  uint16_t accumulatorOrigin()const;
  std::vector<uint8_t> stateBytes()const; // diagnostic fingerprint, not a restore format
  // Full-machine save state. `bind` is invoked while loading, after the configuration flags and the binding kind are read and before the
  // dynamic CPU state is overlaid; it must re-attach the program storage through one of the rebind functions below.
  void snapshot(snap::Ar& a,const std::function<void(VdMcuCpu&)>& bind);
  void rebindProgram(VdMcuProgram& program);
  void rebindProgram(AvdMcuProgram& program);
  void rebindPm(std::span<const uint8_t> code,std::function<bool(uint16_t)> sourceValid);
  bool bound()const;
};
}
