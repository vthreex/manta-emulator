// Isolated bus/ISA proof using original, hash-checked TSP instructions.
// The small prologue is a TEST HARNESS outside the immutable original image;
// it only supplies call registers. It is never installed in connected boot.
#include "peripherals/tsp_packet_window.hpp"
#include "peripherals/tsp_section_dma.hpp"
#include "peripherals/tsp_transport.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
  check(argc==2,"supply the unmodified original TSP image");
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> image(std::istreambuf_iterator<char>(f),{});
  check(manta::sha256(image)=="69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a","original TSP hash mismatch");
  unsigned checks=0;
  for(uint32_t poison:{0u,0xffffffffu}){
    manta::TspPacketWindow window;
    const std::array<uint32_t,4> harness={0x18a00021,0xa8a50200,((uint32_t(0x1e0c-0x10008)/4)&0x03ffffff),0x15000000};
    manta::OpenRisc cpu;
    cpu.read=[&](uint32_t a,unsigned n,bool fetch)->manta::OrWord{
      if(!fetch && a==0x210218)return {poison,0,a};
      uint32_t value=0;
      if(a>=0x10000 && a<0x10010)value=harness[(a-0x10000)/4];
      else{check(a+4<=image.size(),"isolated startup instruction bounds");for(unsigned i=0;i<4;++i)value|=uint32_t(image[a+i])<<(8*i);}
      return {value,0xffffffffu,{}};
    };
    cpu.write=[&](uint32_t a,unsigned n,manta::OrWord value){check(a==0x210218 && n==4 && window.write(a,value),"original startup bit4 RMW must preserve opaque command bits without launching DMA");};
    cpu.reset(0x10000);for(unsigned i=0;i<12 && cpu.pc()!=0x1e18;++i)cpu.step();
    check(cpu.pc()==0x1e18 && !window.occupied() && window.captured==0,"startup release cannot invent a received packet");
  }
  for(unsigned cursor:{0u,4u,5u,31u,184u,187u}){
    manta::TspPacketWindow window;std::array<uint8_t,188> packet{};
    for(unsigned i=0;i<packet.size();++i)packet[i]=uint8_t(i*7+3);packet[0]=0x47;
    check(window.read(0x210214)->value==0,"empty packet window cannot claim packet arrival");
    check(window.capture(packet,7),"actual input must enter an empty window");
    check(!window.capture(packet,8),"occupied window must apply backpressure");
    std::map<uint32_t,uint8_t> memory;
    for(unsigned i=0;i<image.size();++i)memory[i]=image[i];
    const auto word=[&](uint32_t a,uint32_t v){for(unsigned i=0;i<4;++i)memory[a+i]=uint8_t(v>>(8*i));};
    // r1=stack, r11=packet registers, r12=cursor; jump into ORIGINAL FF8.
    const uint32_t harness=0x10000;
    const std::array<uint32_t,8> setup={0x9c203000,0x9d400000,0x19600021,0xa96b0200,0x9d800000u|cursor,
      0x00000000u|((uint32_t(0xff8-(harness+20))/4)&0x03ffffff),0x15000000,0x15000000};
    for(unsigned i=0;i<setup.size();++i)word(harness+4*i,setup[i]);
    word(0x2330,0x210200);
    manta::OpenRisc cpu;unsigned original=0;
    cpu.read=[&](uint32_t a,unsigned n,bool fetch)->manta::OrWord{
      if(!fetch){if(auto v=window.read(a)){check(n==4,"original packet accesses are words");return *v;}}
      manta::OrWord result{0,0,a};for(unsigned i=0;i<n;++i){auto it=memory.find(a+i);if(it!=memory.end()){result.value|=uint32_t(it->second)<<(8*i);result.known|=255u<<(8*i);}}
      return result;
    };
    cpu.write=[&](uint32_t a,unsigned n,manta::OrWord v){
      if(window.write(a,v))return;check(v.known==0xffffffffu && n==4,"isolated stack writes must remain known");word(a,v.value);
    };
    cpu.trace=[&](uint32_t pc,uint32_t instruction){if(pc<image.size()){
      uint32_t expected=0;for(unsigned i=0;i<4;++i)expected|=uint32_t(image[pc+i])<<(8*i);
      check(instruction==expected,"executed instruction differs from original TSP image");++original;}};
    cpu.reset(harness,0x8001,0); // explicit isolated architectural call fixture
    for(unsigned i=0;i<100 && cpu.pc()!=0x1008;++i)cpu.step();
    check(cpu.pc()==0x1008 && original>=15,"original FF8/3C4/1004 packet-window path must execute");
    const auto value=cpu.reg(7);check((value.known&255)==255 && (value.value&255)==packet[cursor],"original load returns actual selected transport byte");
    check((window.read(0x210214)->value&0x100)==0,"original helper must acknowledge the ready bit");
    if(cursor==187)check(window.read(0x210204)->known==255,"bytes beyond the packet must stay invalid");
    window.write(0x210218,{0x10,0xffffffffu,{}});window.write(0x210218,{0,0xffffffffu,{}});
    check(!window.occupied() && window.captured==1 && window.released==1,"original release pulse retires the actual packet");
    ++checks;
  }
  for(bool matches:{false,true}){
    manta::TspTransport transport;std::array<uint8_t,188> packet{};packet[0]=0x47;packet[1]=0x40;packet[3]=0x10;
    transport.regRead=[&](uint32_t a)->manta::OrWord{
      uint32_t value=0;
      if(a<0x2100c0)value=a==0x210000?0x00200000:0x1fff;
      else if(a==0x211004)value=matches?0x47:0x46;
      else if(a==0x211014)value=0xfffffffe;
      else if(a>=0x211014)value=0xffffffffu;
      return {value,0xffffffffu,{}};
    };
    transport.supply(0,packet);
    const std::array<uint32_t,9> harness={0x9c203000,0x19200001,0xa9298000,0x9d400000,0x18a00021,0xa8a50200,
      ((uint32_t(0x710-0x10018)/4)&0x03ffffff),0x15000000,0x15000000};
    manta::OpenRisc cpu;
    cpu.read=[&](uint32_t a,unsigned n,bool fetch)->manta::OrWord{
      if(!fetch && a==0x210214)return *transport.read(a);
      if(!fetch && a==0x2ffc)return {0,~0u,{}};
      uint32_t value=0;if(a>=0x10000 && a<0x10000+4*harness.size())value=harness[(a-0x10000)/4];
      else{check(a+4<=image.size(),"positive-match original instruction bounds");for(unsigned i=0;i<4;++i)value|=uint32_t(image[a+i])<<(8*i);}
      return {value,~0u,{}};
    };
    cpu.write=[](uint32_t,unsigned,manta::OrWord){throw std::runtime_error("unexpected write in original positive-match return path");};
    cpu.reset(0x10000,0x8001,0);for(unsigned i=0;i<30 && cpu.pc()!=0x18000;++i)cpu.step();
    check(cpu.pc()==0x18000 && cpu.reg(3).known==0xffffffffu && cpu.reg(3).value==unsigned(matches),
      "original710/714/71C may consume the known positive-match bit without promoting unrelated status bits");
  }
  for(bool alias:{false,true})for(bool wrap:{false,true}){
    // The ORIGINAL428 helper launches DMA, waits for completion, checks Cur
    // movement (including its wrap path) and returns its own success value.
    manta::TspTransport transport;std::array<uint8_t,188> packet{};packet.fill(0xff);packet[0]=0x47;packet[1]=0x40;packet[2]=0;packet[3]=0x10;
    const std::string body="123456789";uint32_t crc=0xffffffffu;
    for(unsigned i=0;i<body.size();++i){packet[5+i]=uint8_t(body[i]);crc=manta::TspSectionDma::crcByte(crc,packet[5+i]);}
    check(crc==0x0376e6e7,"MPEG CRC arithmetic must agree with the independent check value");
    for(unsigned i=0;i<4;++i)packet[14+i]=uint8_t(crc>>(24-8*i));
    std::map<uint32_t,uint8_t> memory;for(unsigned i=0;i<image.size();++i)memory[i]=image[i];
    const auto store=[&](uint32_t a,unsigned n,uint32_t v){for(unsigned i=0;i<n;++i)memory[a+i]=uint8_t(v>>(8*i));};
    const auto load=[&](uint32_t a,unsigned n){manta::OrWord result{0,0,a};for(unsigned i=0;i<n;++i){auto it=memory.find(a+i);if(it!=memory.end()){result.value|=uint32_t(it->second)<<(8*i);result.known|=255u<<(8*i);}}return result;};
    const uint32_t section=0x211000,start=alias?0x80100000:0x100000,end=start+256,initial=wrap?end-8:start;
    const auto physical=manta::TspSectionDma::miuAddress(start);
    store(section+0x24,4,start);store(section+0x28,4,end);store(section+0x2c,4,wrap?start+32:start);
    store(section+0x34,4,initial);store(0x2338,4,section);store(0x2330,4,0x210200);
    for(unsigned i=0;i<48;++i)store(0x210000+4*i,4,i?0x1fff:0x00200000);
    for(unsigned i=0;i<4;++i)store(section+0x14+4*i,4,0xffffffffu);
    transport.regRead=[&](uint32_t a){return load(a,4);};
    transport.regWrite=[&](uint32_t a,uint32_t v){store(a,4,v);};
    transport.memoryWritable=[&](uint32_t a,size_t n){return a>=physical && uint64_t(a)+n<=physical+256;};
    transport.memoryWrite=[&](uint32_t a,std::span<const uint8_t> bytes){for(unsigned i=0;i<bytes.size();++i)store(a+i,1,bytes[i]);};
    check(transport.supply(0,packet),"actual TS bytes must enter the guest-programmed PID/section route");
    transport.write(0x210214,{0,0xffffffffu,{}});
    for(unsigned fragment=0;fragment<2;++fragment){
      const auto offset=fragment?12u:5u,count=fragment?6u:7u,command=fragment?9u:0x89u;
      store(0x2348,4,command);store(0x2344,4,count);store(0x210220,4,offset);store(0x210224,4,count);
      const uint32_t harness=0x10000,stop=0x18000;
      const std::array<uint32_t,10> setup={0x9c203000,0x19200001,0xa9298000,0x9d400000,0x9d600000,0x9d800000,0x9da00000,
        ((uint32_t(0x428-(harness+28))/4)&0x03ffffff),0x15000000,0x15000000};
      for(unsigned i=0;i<setup.size();++i)store(harness+4*i,4,setup[i]);
      manta::OpenRisc cpu;
      cpu.read=[&](uint32_t a,unsigned n,bool fetch)->manta::OrWord{
        if(!fetch)if(auto value=transport.read(a))return *value;
        return load(a,n);
      };
      cpu.write=[&](uint32_t a,unsigned n,manta::OrWord value){
        if(transport.write(a,value))return;
        check(value.known==0xffffffffu,"stack/global writes must remain known");store(a,n,value.value);
      };
      cpu.trace=[&](uint32_t pc,uint32_t instruction){if(pc<image.size()){
        uint32_t expected=0;for(unsigned i=0;i<4;++i)expected|=uint32_t(image[pc+i])<<(8*i);
        check(instruction==expected,"DMA helper executes a modified original instruction");}};
      cpu.reset(harness,0x8001,0);for(unsigned i=0;i<250 && cpu.pc()!=stop;++i)cpu.step();
      check(cpu.pc()==stop && cpu.reg(3).known==0xffffffffu && cpu.reg(3).value==1,"original DMA helper must return its own success after actual byte movement");
      check(!(transport.read(0x210214)->value&1),"completion must be acknowledged by the original instruction");
    }
    check(transport.dmaBytes==13 && transport.dmaTransfers==2 && load(section+0x3c,4).value==0,"two actual fragments must produce a zero-residue MPEG section CRC");
    for(unsigned i=0;i<13;++i){const auto address=physical+(initial-start+i)%256;check(load(address,1).value==packet[5+i],"DMA ring contains the actual packet fragment bytes");}
    ++checks;
  }
  std::cout<<"{\"result\":\"PASS\",\"original_startup_RMW_cases\":2,\"original_packet_window_cases\":6,\"original_DMA_cases\":"<<checks-6<<",\"scope\":\"isolated original TSP packet-window and section-DMA instructions; connected reception not asserted\"}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
