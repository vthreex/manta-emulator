#pragma once
#include "peripherals/vd_mcu_program.hpp"
#include <functional>
#include <memory>
#include <string>
#include <stdexcept>
namespace manta {
struct VdHardwareBoundary:std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Bounded auxiliary startup experiment, not a complete MStar MCU hardware
// model. Original code storage is shared with the guest programming port.
class VdMcuCpu {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  VdMcuCpu();
  ~VdMcuCpu();
  VdMcuCpu(const VdMcuCpu&)=delete;
  std::function<void(uint16_t,uint8_t,const std::string&)> instruction;
  std::function<void(uint16_t,bool,uint8_t)> external;
  std::function<void(uint8_t,bool,uint8_t)> specialRegister;
  bool deferSfr8eWrite=false;
  std::function<void(uint8_t)> deferredSfr8e;
  bool deferResetKeyWrites=false;
  std::function<void(uint16_t,uint8_t)> deferredResetKey;
  void startup(VdMcuProgram& program,uint64_t cycleBudget=10000);
  uint64_t instructions()const;
  uint64_t cycles()const;
  uint16_t pc()const;
  uint8_t stack()const;
  uint8_t accumulator()const;
  uint8_t status()const;
};
}
