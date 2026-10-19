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
void gopExportTests(){
  using namespace manta;
  // 3x2 window inside a 10-byte pitch: opaque black, transparent white, pure red/green/blue (5-bit), padding must be ignored.
  std::vector<uint8_t> mem(20,0xee);
  auto put=[&](unsigned row,unsigned col,uint16_t v){mem[row*10+col*2]=uint8_t(v);mem[row*10+col*2+1]=uint8_t(v>>8);};
  put(0,0,0x8000);put(0,1,0x7fff);put(0,2,0xfc00);put(1,0,0x83e0);put(1,1,0x801f);put(1,2,0x0000);
  auto bmp=argb1555WindowToBmp32(mem,10,3,2);
  require(bmp.size()==54+3*2*4 && bmp[0]=='B' && bmp[1]=='M',"BMP header and size");
  require(bmp[18]==3 && bmp[22]==0xfe && bmp[23]==0xff && bmp[24]==0xff && bmp[25]==0xff,"width 3, negative height (top-down) -2");
  auto px=[&](unsigned i){return std::array<uint8_t,4>{bmp[54+i*4],bmp[55+i*4],bmp[56+i*4],bmp[57+i*4]};};
  require((px(0)==std::array<uint8_t,4>{0,0,0,255}),"8000 = opaque black");
  require((px(1)==std::array<uint8_t,4>{255,255,255,0}),"7FFF = transparent white");
  require((px(2)==std::array<uint8_t,4>{0,0,255,255}),"FC00 = opaque red in BGRA byte order");
  require((px(3)==std::array<uint8_t,4>{0,255,0,255}),"83E0 = opaque green");
  require((px(4)==std::array<uint8_t,4>{255,0,0,255}),"801F = opaque blue");
  bool rejected=false;
  try{argb1555WindowToBmp32(mem,10,3,3);}catch(const std::exception&){rejected=true;}
  require(rejected,"geometry beyond the observed memory is refused");
  rejected=false;
  try{argb1555WindowToBmp32(mem,4,3,1);}catch(const std::exception&){rejected=true;}
  require(rejected,"pitch smaller than a row is refused");
}
void gopPreviewTests(){
  using namespace manta;
  auto layer=[](unsigned bank,unsigned win,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t pitch,unsigned fmt){
    GopLayer l;l.bank=bank;l.window=win;l.x=x;l.y=y;l.width=w;l.height=h;l.pitch=pitch;l.format=fmt;l.memory.assign(size_t(pitch)*h,0);return l;};
  auto put=[](GopLayer& l,unsigned px,unsigned py,uint16_t v){size_t o=size_t(py)*l.pitch+px*2;l.memory[o]=uint8_t(v);l.memory[o+1]=uint8_t(v>>8);};
  require(argb1555ToBgra(0x8000)==0xff000000u && argb1555ToBgra(0x7fff)==0x00ffffffu && argb1555ToBgra(0xfc00)==0xffff0000u && argb1555ToBgra(0x83e0)==0xff00ff00u && argb1555ToBgra(0x801f)==0xff0000ffu,"ARGB1555 -> BGRA words");
  {
    auto a=layer(4,1,10,5,3,2,10,6);   // padding bytes after each row must be ignored
    put(a,0,0,0x8000);put(a,1,0,0xfc00);put(a,2,1,0x801f);put(a,0,1,0x7fff);
    for(size_t r=0;r<2;++r)for(size_t c=6;c<10;++c)a.memory[r*10+c]=0xee;
    std::vector<GopLayer> v{a};auto f=composeGopPreview(v);
    require(f.width==13 && f.height==7 && f.label.find("DIAGNOSTIC GOP PREVIEW")!=std::string::npos,"canvas is the bounding box of composed windows and is labelled diagnostic");
    auto at=[&](unsigned x,unsigned y){return f.bgra[size_t(y)*f.width+x];};
    require(at(10,5)==0xff000000u && at(11,5)==0xffff0000u && at(12,6)==0xff0000ffu,"guest pixels land at the window coordinates");
    require(!f.covered[size_t(6)*f.width+10] && at(10,6)==0 && !f.covered[0],"alpha-clear guest pixels and uncovered canvas stay uncovered; host invents nothing");
    auto f2=composeGopPreview(v);require(f2.checksum==f.checksum,"checksum is deterministic");
    v[0].memory[0]=0x00;v[0].memory[1]=0x80;put(v[0],1,0,0x8001);auto f3=composeGopPreview(v);
    require(f3.checksum!=f.checksum,"checksum follows guest memory changes");
  }
  {
    auto lo=layer(1,0,0,0,4,4,8,6),hi=layer(4,1,2,2,4,4,8,6);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){put(lo,x,y,0xfc00);put(hi,x,y,0x801f);}
    put(hi,0,0,0x0000);   // transparent pixel of the upper window must not erase the lower one
    std::vector<GopLayer> v{hi,lo};auto f=composeGopPreview(v);
    require(f.width==6 && f.height==6 && f.bgra[2*6+2]==0xffff0000u && f.bgra[2*6+3]==0xff0000ffu && f.bgra[5*6+5]==0xff0000ffu,"windows compose in (bank,window) order; transparent upper pixel keeps the lower one (priority is an assumption)");
  }
  {
    auto a=layer(4,1,1900,1070,40,40,80,6);for(unsigned y=0;y<40;++y)for(unsigned x=0;x<40;++x)put(a,x,y,0x8000);
    std::vector<GopLayer> v{a};auto f=composeGopPreview(v);
    require(f.width==1920 && f.height==1080 && f.covered[size_t(1079)*1920+1919] && f.description.find("composed")!=std::string::npos,"windows are clipped to the panel canvas");
  }
  {
    std::vector<GopLayer> v{layer(4,0,0,0,4,4,8,5),layer(4,1,0,0,4,4,8,6),layer(1,0,0,0,4,4,6,6),layer(1,1,5000,0,4,4,8,6)};
    v[1].invalidBytes=3;
    auto f=composeGopPreview(v);
    require(f.empty() && f.description.find("format not decoded")!=std::string::npos && f.description.find("unknown bytes")!=std::string::npos &&
            f.description.find("geometry exceeds")!=std::string::npos && f.description.find("outside the panel")!=std::string::npos,"unsupported formats, unknown bytes, bad geometry and off-panel windows are skipped with reasons");
  }
  {
    GopPreviewStore store;require(!store.latest() && !store.finished(),"store starts empty");
    GopPreviewFrame a;a.width=1;a.height=1;a.bgra={0xff000000u};a.covered={1};store.publish(a);
    auto first=store.latest();a.bgra[0]=0xffffffffu;store.publish(a);
    require(first->bgra[0]==0xff000000u && store.latest()->sequence==2 && first->sequence==1,"published frames are immutable snapshots with increasing sequence");
    store.setFinished();require(store.finished(),"finish flag");
  }
}
void geUniformStretchTests(){
  using namespace manta;
  static const std::pair<unsigned,uint16_t> connected[]={{0x00,7},{0x01,0x11},{0x34,0x0c09},{0x20,0x6c80},{0x21,0x03d4},{0x26,0xd880},{0x27,0x00dd},{0x30,8},{0x33,0x0aa0},
    {0x55,0},{0x56,0x54f},{0x57,0xb4},{0x58,0xf7},{0x59,0x40},{0x5e,0x1807},{0x5f,0x1c79},{0x64,0x0c},{0x65,0x08f0},{0x68,0},{0x69,0xb4},{0x6a,0x54f},{0x6b,0xf7},
    {0x6c,0},{0x6d,0},{0x6e,4},{0x6f,0x21},{0x11,0xff01},{0x12,0x0100},{0x13,0x00ff}};
  std::map<unsigned,std::pair<uint16_t,uint16_t>> regs;
  for(auto [i,v]:connected)regs[i]={v,i==1?uint16_t(0x372):uint16_t(0xffff)};
  auto get=[&](unsigned i){return regs[i];};
  std::vector<uint8_t> sourceMemory(38*8);   // the 4x38 bitmap at 0x03D46C80 (rows of 8 bytes)
  for(size_t i=0;i<sourceMemory.size();i+=2){sourceMemory[i]=0xd8;sourceMemory[i+1]=0x46;}
  auto read=[&](uint32_t address,size_t size){
    uint32_t base=0x03d46c80;
    if(address<base || address+size>base+sourceMemory.size())throw std::runtime_error("read outside the fixture");
    return std::vector<uint8_t>(sourceMemory.begin()+(address-base),sourceMemory.begin()+(address-base)+size);};
  auto request=GeStretchRequest::decode(0x8040,get);
  auto plan=GeUniformStretch::decode(request,GeUniformStretch::Alpha::OpaqueSource,read);
  require(!plan.empty && plan.left==0 && plan.right==1359 && plan.top==180 && plan.bottom==247 && plan.pixel==0xc6d8 && plan.sourceWords==4*38,"opaque-source plan: whole 1360x68 destination, alpha bit forced on, all 4x38 source words inspected");
  std::vector<uint8_t> memory(2720*248+16,0xee);   // destination area; pitch padding must stay untouched
  uint32_t destBase=0x00ddd880;
  auto writable=[&](uint32_t address,size_t size){return address>=destBase && address+size<=destBase+memory.size();};
  auto write=[&](uint32_t address,std::span<const uint8_t> bytes){std::copy(bytes.begin(),bytes.end(),memory.begin()+(address-destBase));};
  plan.execute(writable,write);
  size_t first=180*2720,last=247*2720+1359*2;
  require(memory[first]==0xd8 && memory[first+1]==0xc6 && memory[last]==0xd8 && memory[last+1]==0xc6,"exact destination bytes d8 c6 at the first and last pixel");
  require(memory[first-1]==0xee && memory[179*2720]==0xee && memory[248*2720]==0xee && memory[248*2720+5]==0xee,"pixels above and below the destination rectangle stay untouched");
  uint64_t changed=0;for(auto b:memory)changed+=b!=0xee;
  require(changed==1360ull*68*2,"exactly 1360x68 pixels (2 bytes each) written");
  auto alpha=GeUniformStretch::decode(request,GeUniformStretch::Alpha::AlphaBit,read);
  require(alpha.empty,"alpha-bit hypothesis with a clear alpha bit writes nothing");
  regs[0x56]={0x100,0xffff};regs[0x57]={0xc0,0xffff};
  auto clipped=GeUniformStretch::decode(GeStretchRequest::decode(0x8040,get),GeUniformStretch::Alpha::OpaqueSource,read);
  require(clipped.left==0 && clipped.right==0x100 && clipped.top==0xc0 && clipped.bottom==247,"destination is clipped by the programmed clip rectangle");
  regs[0x56]={0x54f,0xffff};regs[0x57]={0xb4,0xffff};
  sourceMemory[37*8+6]=0xd9;   // one different word in the 38th row: beyond the programmed 33 rows but inside the scale height
  rejects([&]{GeUniformStretch::decode(GeStretchRequest::decode(0x8040,get),GeUniformStretch::Alpha::OpaqueSource,read);});
  sourceMemory[37*8+6]=0xd8;
  regs[0x34]={0x0c08,0xffff};
  rejects([&]{GeUniformStretch::decode(GeStretchRequest::decode(0x8040,get),GeUniformStretch::Alpha::OpaqueSource,read);});
}
void geI2GlyphTests(){
  using namespace manta;
  // SELF-CONSISTENCY tests of the EXPLORATORY hypotheses H1..H5 (not hardware evidence): the register block is the one the original
  // ZUI programmed for its first text glyph; expected words are derived by hand from H4 (rounding to nearest, truncation to 5 bits).
  static const std::pair<unsigned,uint16_t> glyphRegs[]={{0x00,0x45},{0x01,0x01},{0x34,0x0c01},{0x20,0x6700},{0x21,0x00cb},{0x26,0xd880},{0x27,0x00dd},{0x30,8},{0x33,0x0aa0},
    {0x35,0x0101},{0x36,0xff01},{0x37,0xffff},{0x38,0xaaff},{0x39,0xffff},{0x3a,0xccff},{0x3b,0xffff},{0x3c,0xffff},{0x14,0x0101},{0x15,0x0101},{0x16,0},{0x17,0},
    {0x11,0xff02},{0x13,0x00ff},{0x55,0x57},{0x56,0x113},{0x57,0xb4},{0x58,0xef},{0x59,0x40},{0x64,0x1000},{0x65,0x1000},{0x68,0x5a},{0x69,0xbe},{0x6a,0x66},{0x6b,0xe5},
    {0x6c,0},{0x6d,0},{0x6e,13},{0x6f,35}};
  std::map<unsigned,std::pair<uint16_t,uint16_t>> regs;
  for(auto [i,v]:glyphRegs)regs[i]={v,i==1?uint16_t(0x372):uint16_t(0xffff)};
  auto get=[&](unsigned i){return regs[i];};
  std::vector<uint8_t> sourceMemory(8*40,0);   // pitch 8, 40 rows
  sourceMemory[0]=0x1b;                        // row 0: indices 0,1,2,3
  sourceMemory[1]=0xe4;                        // row 0: indices 3,2,1,0
  sourceMemory[8*10+3]=0x40;                   // row 10, pixel 12: index 1 (last column of a 13-wide glyph)
  auto read=[&](uint32_t address,size_t size){
    uint32_t base=0x00cb6700;
    if(address<base || address+size>base+sourceMemory.size())throw std::runtime_error("read outside the fixture");
    return std::vector<uint8_t>(sourceMemory.begin()+(address-base),sourceMemory.begin()+(address-base)+size);};
  auto glyph=GeI2Glyph::decode(0x40,get,read);
  require(glyph.width==13 && glyph.rows==40 && glyph.programmedSourceRows==35 && glyph.palette[0]==0xff010101u && glyph.palette[1]==0xaaffffffu && glyph.palette[2]==0xccffffffu && glyph.palette[3]==0xffffffffu,"decode: 13x40 destination, programmed height 35, palette words low-half-first");
  require(glyph.keyHigh==0x01010101u && glyph.keyLow==0 && glyph.keyed(0xff010101u) && !glyph.keyed(0xaaffffffu) && !glyph.keyed(0xff020101u),"key range is compared on RGB only; entry 0 is keyed, white entries are not");
  require(glyph.index(0,0)==0 && glyph.index(1,0)==1 && glyph.index(2,0)==2 && glyph.index(3,0)==3 && glyph.index(4,0)==3 && glyph.index(7,0)==0 && glyph.index(12,10)==1,"2-bit pixels are unpacked most-significant pair first");
  std::vector<uint8_t> memory(2720*248,0);
  for(size_t i=0;i<memory.size();i+=2){memory[i]=0xd8;memory[i+1]=0xc6;}   // destination bar colour 0xC6D8
  uint32_t destBase=0x00ddd880;
  auto dread=[&](uint32_t address,size_t size){return std::vector<uint8_t>(memory.begin()+(address-destBase),memory.begin()+(address-destBase)+size);};
  auto writable=[&](uint32_t address,size_t size){return address>=destBase && address+size<=destBase+memory.size();};
  auto write=[&](uint32_t address,std::span<const uint8_t> bytes){std::copy(bytes.begin(),bytes.end(),memory.begin()+(address-destBase));};
  glyph.execute(dread,writable,write);
  auto word=[&](unsigned x,unsigned y){size_t o=size_t(y)*2720+size_t(x)*2;return uint16_t(memory[o]|(memory[o+1]<<8));};
  require(word(0x5a,0xbe)==0xc6d8,"index 0 (keyed) leaves the destination untouched");
  require(word(0x5b,0xbe)==0xef9d && word(0x5c,0xbe)==0xf7de && word(0x5d,0xbe)==0xffff && word(0x5e,0xbe)==0xffff && word(0x5f,0xbe)==0xf7de && word(0x60,0xbe)==0xef9d && word(0x61,0xbe)==0xc6d8,"indices 1,2,3 blend white with alpha AA/CC/FF (hypothesis H4) over 0xC6D8");
  require(word(0x66,200)==0xef9d && word(0x67,200)==0xc6d8,"column 12 of row 10 is written, the column after the glyph is not");
  uint64_t changed=0;for(size_t i=0;i<memory.size();i+=2)changed+=(memory[i]|(memory[i+1]<<8))!=0xc6d8;
  require(changed==7,"exactly the 7 non-keyed glyph pixels changed (6 in row 0, 1 in row 10)");
  // clipping: right clip inside the glyph
  regs[0x56]={0x5d,0xffff};
  auto clipped=GeI2Glyph::decode(0x40,get,read);
  require(clipped.left==0x5a && clipped.right==0x5d,"destination clip is honoured");
  regs[0x56]={0x113,0xffff};
  // rows hidden by the height-5 quirk must be blank
  sourceMemory[8*37+1]=0x40;
  rejects([&]{GeI2Glyph::decode(0x40,get,read);});
  sourceMemory[8*37+1]=0;
  regs[0x6f]={34,0xffff};rejects([&]{GeI2Glyph::decode(0x40,get,read);});
  regs[0x6f]={35,0xffff};
  regs[0x00]={0x41,0xffff};rejects([&]{GeI2Glyph::decode(0x40,get,read);});   // no blend
  regs[0x00]={0x45,0xffff};
  regs[0x34]={0x0c02,0xffff};rejects([&]{GeI2Glyph::decode(0x40,get,read);});   // another source format
  regs[0x34]={0x0c01,0xffff};
  regs[0x38].second=0xfffe;rejects([&]{GeI2Glyph::decode(0x40,get,read);});   // unknown palette bit
  regs[0x38].second=0xffff;
  regs[0x01]={0x11,0x372};rejects([&]{GeI2Glyph::decode(0x40,get,read);});   // stretch mode
  regs[0x01]={0x01,0x372};
  require(GeI2Glyph::decode(0x40,get,read).width==13,"the restored registers decode again");
}
void geBlendCopyTests(){
  using namespace manta;
  // SELF-CONSISTENCY tests of the EXPLORATORY blend-copy hypotheses B1..B3; the registers are the ones the original ZUI programmed for its first surface copy.
  static const std::pair<unsigned,uint16_t> regsList[]={{0x00,5},{0x01,0x01},{0x34,0x0c09},{0x20,0xd880},{0x21,0x00dd},{0x26,0x2380},{0x27,0x00e8},{0x30,0xaa0},{0x33,0xaa0},
    {0x11,0xff00},{0x13,0x00ff},{0x55,0},{0x56,0x54f},{0x57,0},{0x58,0xf7},{0x59,0x40},{0x64,0x1000},{0x65,0x1000},{0x68,0},{0x69,0xf0},{0x6a,0x54f},{0x6b,0xf7},{0x6c,0},{0x6d,0},{0x6e,0x550},{0x6f,3}};
  std::map<unsigned,std::pair<uint16_t,uint16_t>> regs;
  for(auto [i,v]:regsList)regs[i]={v,i==1?uint16_t(0x372):uint16_t(0xffff)};
  auto get=[&](unsigned i){return regs[i];};
  const uint32_t srcBase=0x00ddd880,dstBase=0x00e82380+240*2720;
  std::vector<uint8_t> src(8*2720,0),dst(8*2720,0);
  for(size_t i=0;i<src.size();i+=2){src[i]=0x00;src[i+1]=0x80;dst[i]=0x00;dst[i+1]=0x80;}   // both black 0x8000
  auto read=[&](uint32_t address,size_t size){
    if(address>=srcBase && address+size<=srcBase+src.size())return std::vector<uint8_t>(src.begin()+(address-srcBase),src.begin()+(address-srcBase)+size);
    if(address>=dstBase && address+size<=dstBase+dst.size())return std::vector<uint8_t>(dst.begin()+(address-dstBase),dst.begin()+(address-dstBase)+size);
    throw std::runtime_error("read outside the fixtures");};
  auto writable=[&](uint32_t address,size_t size){return address>=dstBase && address+size<=dstBase+dst.size();};
  auto write=[&](uint32_t address,std::span<const uint8_t> bytes){std::copy(bytes.begin(),bytes.end(),dst.begin()+(address-dstBase));};
  auto plan=GeBlendCopy::decode(0x40,get,read);
  require(plan.left==0 && plan.right==1359 && plan.top==240 && plan.bottom==247 && plan.programmedRows==3 && plan.rows==8,"decode: 1360x8 destination at y=240, programmed height 3 (= 8 - 5)");
  plan.execute(writable,write);
  bool allBlack=true;for(size_t i=0;i<dst.size();i+=2)allBlack&=dst[i]==0 && dst[i+1]==0x80;
  require(allBlack,"copying black onto black leaves every word 0x8000 (identical under both height readings)");
  src[0]=0xd8;src[1]=0xc6;                    // source row 0 pixel 0 differs -> copied to destination row 240 pixel 0
  src[2720*2+10]=0x34;src[2720*2+11]=0x12;    // source row 2 pixel 5
  plan=GeBlendCopy::decode(0x40,get,read);plan.execute(writable,write);
  require(dst[0]==0xd8 && dst[1]==0xc6 && dst[2720*2+10]==0x34 && dst[2720*2+11]==0x12 && dst[2]==0 && dst[3]==0x80,"source words are copied unchanged to the same column of the matching destination row");
  src[2720*4+20]=0x77;                        // source row 4 (hidden by the quirk) differs from destination
  rejects([&]{GeBlendCopy::decode(0x40,get,read);});
  src[2720*4+20]=0;
  regs[0x11]={0xff01,0xffff};rejects([&]{GeBlendCopy::decode(0x40,get,read);});   // coefficient CONST, not ONE
  regs[0x11]={0xff00,0xffff};
  regs[0x00]={0x45,0xffff};rejects([&]{GeBlendCopy::decode(0x40,get,read);});     // key enabled
  regs[0x00]={5,0xffff};
  regs[0x6e]={0x54f,0xffff};rejects([&]{GeBlendCopy::decode(0x40,get,read);});    // width mismatch
  regs[0x6e]={0x550,0xffff};
  regs[0x20]={0xe380,0xffff};regs[0x21]={0x00e8,0xffff};regs[0x30]={0xaa0,0xffff};   // source inside the destination surface: overlapping
  rejects([&]{GeBlendCopy::decode(0x40,get,read);});
}
void avdNoiseConstantTests(){
  using namespace manta;
  Riu off;off.symbolicInit=true;off.event=[](auto,auto,auto,auto,auto){};
  require(off.knownBits(0x1f206a04,1)==0,"AFEC noise magnitude stays invalid without the hypothesis");
  Riu r;r.symbolicInit=true;r.event=[](auto,auto,auto,auto,auto){};r.configureExploratoryAvdNoise(0x2a);
  require(r.knownBits(0x1f206a04,1)==255 && r.knownBits(0x1f206a05,1)==0 && r.knownBits(0x1f206a03,1)==0 && r.knownBits(0x1f206a08,1)==0,"only the noise byte becomes known");
  auto v=r.read(0x1f206a04,1,0x80503138,0xff,1);r.stopReason.clear();
  require(v==0x2a && r.avdNoiseReads()==1,"constant returned and counted");
  bool rejected=false;
  try{r.write(0x1f206a04,1,0,0x80503000,0xff,2);}catch(const std::exception&){rejected=true;}
  require(rejected,"a guest write cannot manufacture the noise magnitude");
}
void avdHtotalConstantTests(){
  using namespace manta;
  Riu off;off.symbolicInit=true;off.event=[](auto,auto,auto,auto,auto){};
  require(off.knownBits(0x1f206b8d,1)==0 && off.knownBits(0x1f206b90,1)==0,"published HTotal stays invalid without the hypothesis");
  Riu r;r.symbolicInit=true;r.event=[](auto,auto,auto,auto,auto){};r.configureExploratoryAvdHtotal(0x1234);
  require(r.knownBits(0x1f206b8d,1)==255 && r.knownBits(0x1f206b90,1)==255,"both published HTotal bytes become known");
  require(r.knownBits(0x1f206b8c,1)==0 && r.knownBits(0x1f206b8e,1)==0 && r.knownBits(0x1f206b91,1)==0 && r.knownBits(0x1f206b94,1)==0,"neighbouring AFEC bytes (including the co-channel word) stay invalid");
  auto lo=r.read(0x1f206b8d,1,0x805030fc,0xff,1);r.stopReason.clear();
  auto hi=r.read(0x1f206b90,1,0x805030f8,0xff,2);r.stopReason.clear();
  require(lo==0x34 && hi==0x12 && r.avdHtotalReads()==2,"low byte at BF206B8D, high byte at BF206B90, reads counted");
  bool rejected=false;
  try{r.write(0x1f206b90,1,0,0x80503000,0xff,3);}catch(const std::exception&){rejected=true;}
  require(rejected,"a guest write cannot manufacture the published HTotal");
}
void vifCrLockBracketTests(){
  using namespace manta;
  Riu off;off.symbolicInit=true;off.event=[](auto,auto,auto,auto,auto){};
  require(off.knownBits(0x1f224471,1)==0 && off.knownBits(0x1f224470,2)==0,"without the bracket the VIF carrier-lock status and its neighbour stay invalid");
  for(unsigned bit:{0u,1u}){
    Riu r;r.symbolicInit=true;r.event=[](auto,auto,auto,auto,auto){};r.configureDiagnosticVifCrLock(bit);
    require(r.knownBits(0x1f224471,1)==1 && r.knownBits(0x1f224470,1)==0,"only bit 0 of 0x1F224471 becomes known; CR_FOE (0x1F224470) stays invalid");
    require(r.knownBits(0x1f224470,2)==0x0100 && r.knownBits(0x1f224470,4)==0x00000100,"halfword/word reads expose exactly that one bit at its byte lane");
    auto v=r.read(0x1f224471,1,0x80509a94,0xff,10);r.stopReason.clear();
    require((v&1)==bit && r.vifCrLockReads()==1,"bracketed value is returned and counted");
    require(r.knownBits(0x1f224440,1)==0 && r.knownBits(0x1f224472,1)==0,"neighbouring VIF registers are untouched");
    bool rejected=false;
    try{r.write(0x1f224471,1,bit,0x80509a94,0xff,11);}catch(const std::exception&){rejected=true;}
    require(rejected,"a guest write cannot manufacture the hardware-owned lock status");
  }
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
