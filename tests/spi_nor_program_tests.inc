void spiNorProgramTests(){
  using namespace manta;
  auto makeStock=[]{std::vector<uint8_t> v(0x800000);for(size_t i=0;i<v.size();++i)v[i]=uint8_t(i*7+3);return v;};
  Flash flash(makeStock());
  const auto stockDigest=flash.stockDigest();
  auto wire=[](SpiNor& n,Flash& f){
    n.flashProgram=[&f](uint32_t a,std::span<const uint8_t> b){return f.programAnd(a,b);};
    n.flashErase=[&f](uint32_t a,uint32_t sz){f.eraseRange(a,sz);};
    n.flashByte=[&f](uint32_t a){return f.read(a);};};
  // Default (write support off): program/erase/WRSR are ignored exactly as before; no busy interval, no errors.
  {
    Flash off(makeStock());SpiNor n;wire(n,off);
    auto cmd=[&](std::initializer_list<uint8_t> bytes,uint64_t tick){n.now=tick;n.select();for(auto b:bytes)n.transmit(b);n.deselect();};
    cmd({6},0);cmd({0x01,0xbc},1);cmd({6},2);cmd({0x02,0,0x10,0,0x00},3);cmd({6},4);cmd({0x20,0,0x10,0},5);cmd({0xc7},6);
    n.now=7;n.select();n.transmit(5);auto s=n.receive();n.deselect();
    require(n.error.empty() && off.overlaySectors()==0 && n.programs==0 && n.erases==0 && !n.busy() && !(s.value&1),
      "with write support off the NOR ignores WRSR/program/erase and never reports BUSY");
  }
  SpiNor nor;wire(nor,flash);nor.writeSupport=true;
  auto run=[&](std::initializer_list<uint8_t> bytes,uint64_t tick){
    nor.now=tick;nor.select();for(auto b:bytes)nor.transmit(b);nor.deselect();};
  auto status=[&](uint64_t tick){nor.now=tick;nor.select();nor.transmit(5);auto s=nor.receive();nor.deselect();return s;};
  require(flash.read(0x1000)==makeStock()[0x1000] && flash.overlaySectors()==0,"guest reads initially match the stock flash image");
  // Program without write enable is silently ignored by the device and counted.
  run({0x02,0x00,0x10,0x00,0x00},0);
  require(nor.ignoredWrites==1 && nor.programs==0 && flash.overlaySectors()==0 && nor.error.empty(),"page program without WEL is ignored, not applied");
  // The block-protect state is unknown until the guest writes the status register: stop rather than guess.
  run({0x06},5);run({0x02,0x00,0x10,0x00,0x00},6);
  require(nor.error.find("unknown block-protect state")!=std::string::npos && flash.overlaySectors()==0,"program with unknown protection state stops");
  nor.error.clear();
  // WRSR: original boot sequence 06 01 BC (all blocks protected), then an unprotecting write.
  run({0x01,0xbc},7);require(nor.statusKnown() && nor.statusBits()==0xbc && !nor.writeEnabled(),"WRSR 0xBC stores SRP0/TB/BP bits and consumes WEL");
  require(status(8).value&1,"BUSY while the status register is written");
  uint64_t t=7+nor.statusWriteTicks+1;
  run({0x06},t);run({0x02,0x00,0x10,0x00,0xaa},t+10);
  require(nor.programs==0 && nor.blockedByProtection==1 && flash.overlaySectors()==0 && nor.error.empty() && !nor.writeEnabled(),
    "program into an all-protected array is blocked by BP2:0=111 and counted");
  run({0x06},t+20);run({0x01,0x00},t+30);require(nor.statusBits()==0,"unprotecting WRSR");
  t+=nor.statusWriteTicks+100;
  // The status the original firmware writes before flash writes: 0xB8 = SRP0, TB, BP=110 -> lowest 2 MiB protected.
  run({0x06},t);run({0x01,0xb8},t+5);t+=nor.statusWriteTicks+100;
  run({0x06},t);run({0x02,0x00,0x1f,0xff,0x00},t+5);
  require(nor.blockedByProtection==2 && flash.overlaySectors()==0,"TB=1,BP=110 protects the lowest 2 MiB");
  t+=100;run({0x06},t);run({0x02,0x20,0x00,0x00,0xee},t+5);
  require(nor.programs==1 && nor.blockedByProtection==2 && flash.read(0x200000)==uint8_t(makeStock()[0x200000]&0xee),"the first byte above the protected 2 MiB is writable");
  t+=nor.programTicks+100;run({0x06},t);run({0x20,0x00,0x10,0x00},t+5);
  require(nor.erases==0 && nor.blockedByProtection==3,"erase inside the protected region is blocked and counted");
  t+=100;run({0x06},t);run({0x01,0x58},t+5);   // SEC=1 pattern: not modeled
  t+=nor.statusWriteTicks+100;run({0x06},t);run({0x02,0x20,0x00,0x00,0x00},t+5);
  require(nor.error.find("SEC block-protect pattern not modeled")!=std::string::npos,"SEC=1 protection patterns stop instead of being guessed");
  nor.error.clear();t+=100;run({0x06},t);run({0x01,0x00},t+5);t+=nor.statusWriteTicks+100;
  // WREN then program: WEL set on CS rise, applied on CS rise, WEL cleared, device busy.
  run({0x06},t);require(nor.writeEnabled(),"WREN sets WEL on the CS rising edge");
  auto before=flash.read(0x1000);auto programs0=nor.programs,bytes0=nor.programmedBytes;
  run({0x02,0x00,0x10,0x00,0x0f,0xf0},t+20);
  require(nor.programs==programs0+1 && !nor.writeEnabled() && nor.programmedBytes==bytes0+2,"page program consumes WEL");
  require(flash.read(0x1000)==uint8_t(before&0x0f) && flash.read(0x1001)==uint8_t(makeStock()[0x1001]&0xf0),
    "programmed bytes obey NOR rules: result is old AND new");
  auto busy=status(t+21);require((busy.value&1) && (busy.value&2)==0 && busy.known==3,"RDSR reports BUSY (known) and cleared WEL while programming");
  auto idle=status(t+20+nor.programTicks);require(!(idle.value&1),"BUSY clears after the program interval");
  // Commands are ignored while busy, and a read command errors.
  auto ignored0=nor.ignoredWrites;
  run({0x02,0x00,0x20,0x00,0x00},t+25);require(nor.ignoredWrites==ignored0+1,"write commands during BUSY are ignored");
  nor.now=t+25;nor.select();nor.transmit(0x03);nor.transmit(0);nor.transmit(0);nor.transmit(0);
  nor.receive();require(!nor.error.empty(),"array read while BUSY is rejected");nor.error.clear();nor.deselect();
  // Page wrap: address 0xFE with 4 bytes -> FE,FF then 00,01 of the same page.
  t+=nor.programTicks*2;
  flash.eraseRange(0x2000,4096);run({0x06},t);run({0x02,0x00,0x20,0xfe,0x11,0x22,0x33,0x44},t);
  require(flash.read(0x20fe)==0x11 && flash.read(0x20ff)==0x22 && flash.read(0x2000)==0x33 && flash.read(0x2001)==0x44 && flash.read(0x2100)==0xff,
    "page program wraps inside the 256-byte page");
  // A 0->1 request is counted and not honored: the earlier programmed byte cannot be raised again.
  t+=nor.programTicks+1;auto v0=nor.norViolations;run({0x06},t);run({0x02,0x00,0x20,0xfe,0xff},t);
  require(flash.read(0x20fe)==0x11 && nor.norViolations==v0+1,"programming 1 bits over 0 bits changes nothing (and is counted as a request)");
  t+=nor.programTicks+1;run({0x06},t);run({0x02,0x00,0x20,0xfe,0xee},t);
  require(flash.read(0x20fe)==uint8_t(0x11&0xee) && nor.norViolations==v0+2,"a 0->1 request is counted and the cell keeps old AND new");
  // Erase: unaligned address erases its whole 4 KiB sector; neighbours untouched; also needs WEL.
  t+=nor.programTicks+1;
  auto ignoredBefore=nor.ignoredWrites;
  run({0x20,0x00,0x30,0x10},t);require(nor.erases==0 && nor.ignoredWrites==ignoredBefore+1,"erase without WEL is ignored");
  auto neighbourBefore=flash.read(0x2fff);auto afterBefore=flash.read(0x4000);
  run({0x06},t);run({0x20,0x00,0x30,0x10},t);
  require(nor.erases==1 && flash.read(0x3000)==0xff && flash.read(0x3fff)==0xff && flash.read(0x2fff)==neighbourBefore && flash.read(0x4000)==afterBefore,
    "sector erase clears exactly its aligned 4 KiB and leaves adjacent sectors alone");
  require(status(t+1).value&1,"BUSY during erase");
  t+=nor.erase4kTicks+1;run({0x06},t);run({0xd8,0x01,0x23,0x45},t);
  require(nor.erases==2 && flash.read(0x10000)==0xff && flash.read(0x1ffff)==0xff && flash.read(0x20000)==makeStock()[0x20000],"64 KiB block erase aligns down to 0x10000 and stops at 0x1FFFF");
  // Unsupported write-class opcodes stay guarded.
  t+=nor.erase64kTicks+1;run({0x06},t);run({0xc7},t);require(!nor.error.empty(),"chip erase stays guarded");nor.error.clear();
  // Reset restores the device state, including the unknown protection state.
  nor.reset();require(!nor.writeEnabled() && !nor.busy() && !nor.statusKnown(),"reset clears WEL, BUSY and the protection state");
  // Immutability: the stock image digest never changes; modified sectors live only in the overlay.
  require(flash.stockDigest()==stockDigest && flash.overlaySectors()>=3,"stock image is never mutated; guest changes are overlay-only");
  for(uint32_t a:{0u,0xfffu,0x5000u})require(flash.stock()[a]==makeStock()[a],"stock bytes unchanged");
  bool oob=false;try{flash.programAnd(0x7fffff,std::array<uint8_t,2>{1,2});}catch(const std::exception&){oob=true;}
  require(oob,"programming past the end of the 8 MiB part is rejected");
  oob=false;try{flash.eraseRange(0x1001,4096);}catch(const std::exception&){oob=true;}require(oob,"unaligned erase range rejected");
  // Determinism: replaying the identical command stream produces an identical overlay digest.
  auto replay=[&]{Flash f2(makeStock());SpiNor n2;wire(n2,f2);n2.writeSupport=true;
    auto cmd=[&](std::initializer_list<uint8_t> bytes,uint64_t tick){n2.now=tick;n2.select();for(auto b:bytes)n2.transmit(b);n2.deselect();};
    cmd({6},0);cmd({1,0},1);cmd({6},9000000);cmd({0x20,0,0x40,0},9000001);cmd({6},90000000);cmd({0x02,0,0x40,0x10,0x5a,0xa5},90000001);return f2.overlayDigest();};
  require(replay()==replay(),"identical command streams give identical overlays");
  // ISP controller end to end, mirroring the original 8053C914 sequence (WREN; address; 02; data; CE_CLR).
  Flash f3(makeStock());SpiIsp isp;wire(isp.nor,f3);isp.nor.writeSupport=true;isp.flashByte=[&](uint32_t a){return f3.read(a);};
  isp.write(0x18,0x40,0);isp.write(0,0xaaaa,0);uint64_t k=100;
  // unprotect through the ISP in command mode: 06, then 01 with its data byte (what the original boot does)
  isp.write(4,0x06,k);k+=64;isp.write(4,0x01,k);k+=64;isp.write(0x10,0x00,k);k+=64;isp.write(0x20,1,k);
  require(isp.nor.statusKnown() && isp.nor.statusBits()==0 && isp.error.empty(),"ISP WRSR sequence reaches the NOR status register");
  k+=isp.nor.statusWriteTicks+100;
  isp.write(4,0x06,k);k+=64;require(isp.read(0x5c,1,k).value_or(0)==1,"ISP command ready after WREN");
  isp.write(8,0x3456,k);isp.write(0xc,0x01,k);isp.write(4,0x02,k);k+=64*4;
  require(isp.nor.packet().size()==4 && isp.nor.packet()[1]==1 && isp.nor.packet()[2]==0x34 && isp.nor.packet()[3]==0x56,"ISP appends ADDRESS3..1 after a page-program command");
  for(uint8_t b:{0x00,0x12,0x34}){require(isp.read(0x58,1,k).value_or(0)==1,"ISP write FIFO ready");isp.write(0x10,b,k);k+=64;}
  isp.write(0x20,1,k);require(isp.error.empty() && isp.nor.programs==1,"CE_CLR commits the page program through the ISP");
  require(f3.read(0x13456)==0x00 && f3.read(0x13457)==uint8_t(makeStock()[0x13457]&0x12) && f3.read(0x13458)==uint8_t(makeStock()[0x13458]&0x34),
    "ISP page program lands at the address registers' location");
}
void usbNoDeviceTests(){
  using namespace manta;
  Riu off;off.symbolicInit=true;off.event=[](auto,auto,auto,auto,auto){};
  off.write(0x1f207500,2,0x6dc3,0,0xffff,1);
  require(off.knownBits(0x1f200e10,2)==0,"without the hypothesis USBC+0x10 line state stays invalid even with the PHY powered");
  Riu r;r.symbolicInit=true;r.event=[](auto,auto,auto,auto,auto){};r.configureExploratoryUsbNoDevice();
  require(!r.usbPhyPowered() && r.knownBits(0x1f200e10,2)==0,"hypothesis alone defines nothing before the guest powers the PHY");
  r.write(0x1f207500,2,0xefc7,0,0xffff,1);
  require(!r.usbPhyPowered() && r.knownBits(0x1f200e10,2)==0,"a power-down PWRCTRL (bit 15 set) keeps the line state invalid");
  r.write(0x1f207500,2,0x6dc3,0,0xffff,2);
  require(r.usbPhyPowered(),"guest PWRCTRL 6DC3 powers the PHY");
  auto lineState=r.read(0x1f200e10,2,0x803bd910,0xffff,100);r.stopReason.clear();
  require(r.knownBits(0x1f200e10,2)==0xc0 && (lineState&0xc0)==0,"only D-/D+ line-state bits 7:6 become known (zero) in the low byte");
  require(r.knownBits(0x1f200e10,1)==0xc0 && r.knownBits(0x1f200e11,1)==0,"the high byte of the halfword and neighbouring registers stay invalid");
  require(r.knownBits(0x1f200e00,2)==0 && r.knownBits(0x1f200f10,2)==0,"other USBC registers and the second port are untouched while its own PHY is not powered");
  r.write(0x1f207400,2,0xefc7,0,0xffff,3);
  require(!r.usbPhyPowered(1) && r.knownBits(0x1f200f10,2)==0,"the second port stays invalid while its PHY is powered down (boot power-save value EFC7)");
  r.write(0x1f207400,2,0x6dc3,0,0xffff,4);
  auto lineState1=r.read(0x1f200f10,2,0x803bd910,0xffff,200);r.stopReason.clear();
  require(r.usbPhyPowered(1) && r.knownBits(0x1f200f10,2)==0xc0 && (lineState1&0xc0)==0 && r.knownBits(0x1f200e10,2)==0xc0,"each port is defined only by its own guest-powered PHY");
}
