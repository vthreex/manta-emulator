// Isolated original CNI lookup. Not a connected channel-store result.
#include "cpu/engine.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <vector>
#include <iostream>
#include <array>
int main(int argc,char** argv){try{
  if(argc!=2)throw std::runtime_error("supply original decompressed runtime");
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> image(std::istreambuf_iterator<char>(f),{});
  if(manta::sha256(image)!="149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236")throw std::runtime_error("original runtime hash mismatch");
  manta::Engine cpu;cpu.map(0,4096);cpu.map(0x003b0000,0x350000);cpu.map(0x00700000,0x10000);
  cpu.write(0x003b3980,image.data(),image.size());
  auto run=[&](uint32_t pc){cpu.reg(UC_MIPS_REG_SP,0x80708000);cpu.reg(UC_MIPS_REG_RA,0x80000100);
    manta::check(uc_emu_start(cpu.handle,pc,0x80000100,0,200000));
    if(cpu.reg(UC_MIPS_REG_PC)!=0x80000100)throw std::runtime_error("original lookup did not return");};
  cpu.reg(UC_MIPS_REG_A0,0xffff);cpu.reg(UC_MIPS_REG_A1,0x4801);cpu.reg(UC_MIPS_REG_A2,0xffff);cpu.reg(UC_MIPS_REG_A3,0x80700000);
  run(0x805b248c);uint16_t id=0;cpu.read(0x00700000,&id,2);
  if(cpu.reg(UC_MIPS_REG_V0)==255 || id!=512)throw std::runtime_error("TVP1 broadcast ID not found by original lookup");
  cpu.reg(UC_MIPS_REG_A0,id);cpu.reg(UC_MIPS_REG_A1,0x80700010);run(0x805b27fc);
  std::array<char,8> name{};cpu.read(0x00700010,name.data(),name.size());
  if(std::string(name.data(),5)!="TVP1 ")throw std::runtime_error("original stored-name table returned unexpected name");
  std::cout<<R"({"result":"PASS","scope":"isolated original firmware lookup, not channel storage","network_id":18433,"station_id":512,"name":"TVP1"})"<<'\n';
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
