void usbHostTests(){
  using namespace manta;
  auto makeImage=[]{std::vector<uint8_t> v(64*512);for(size_t i=0;i<v.size();++i)v[i]=uint8_t(i*13+5);return v;};
  // ---- bulk-only SCSI device ----
  {
    UsbMassStorage d(makeImage());
    auto cbw=[&](uint32_t tag,uint32_t len,bool in,std::initializer_list<uint8_t> cb){
      std::vector<uint8_t> c(31,0);c[0]='U';c[1]='S';c[2]='B';c[3]='C';for(int i=0;i<4;++i){c[4+i]=uint8_t(tag>>(8*i));c[8+i]=uint8_t(len>>(8*i));}
      c[12]=in?0x80:0;c[14]=uint8_t(cb.size());size_t i=15;for(auto b:cb)c[i++]=b;return c;};
    auto drain=[&]{std::vector<uint8_t> all;for(int guard=0;guard<64;++guard){auto p=d.bulkIn(512);if(p.empty())break;all.insert(all.end(),p.begin(),p.end());}return all;};
    d.bulkOut(cbw(0x11223344,36,true,{0x12,0,0,0,36,0}));
    auto r=drain();
    require(r.size()==36+13 && r[0]==0 && r[1]==0x80 && std::memcmp(&r[8],"MANTA   ",8)==0 && std::memcmp(&r[36],"USBS",4)==0 && r[36+4]==0x44 && r[36+12]==0,
      "bulk-only INQUIRY returns 36 data bytes (removable, MANTA vendor) then a GOOD CSW echoing the tag");
    d.bulkOut(cbw(2,8,true,{0x25,0,0,0,0,0,0,0,0,0}));
    r=drain();
    require(r.size()==8+13 && r[3]==63 && r[6]==2 && r[7]==0,"READ CAPACITY(10) reports last LBA 63 and 512-byte blocks");
    d.bulkOut(cbw(3,1024,true,{0x28,0,0,0,0,2,0,0,2,0}));
    r=drain();
    auto img=makeImage();
    require(r.size()==1024+13 && std::equal(img.begin()+1024,img.begin()+2048,r.begin()) && r[1024+12]==0 && d.readBlocks==2,"READ(10) LBA 2 x2 returns exactly the image blocks");
    d.bulkOut(cbw(4,512,true,{0x28,0,0,0,0,100,0,0,1,0}));
    r=drain();
    require(r.size()==13 && r[12]==1,"READ(10) beyond the capacity fails the command (CSW status 1)");
    d.bulkOut(cbw(5,18,true,{0x03,0,0,0,18,0}));
    r=drain();
    require(r.size()==18+13 && r[2]==5 && r[12]==0x21,"REQUEST SENSE reports ILLEGAL REQUEST / LBA out of range");
    std::vector<uint8_t> blk(512,0xA5);
    d.bulkOut(cbw(6,512,false,{0x2a,0,0,0,0,7,0,0,1,0}));
    d.bulkOut(blk);
    r=drain();
    require(r.size()==13 && r[12]==0 && d.writtenBlocks==1,"WRITE(10) accepts one block and completes GOOD");
    d.bulkOut(cbw(7,512,true,{0x28,0,0,0,0,7,0,0,1,0}));
    r=drain();
    require(r.size()==512+13 && r[0]==0xA5 && r[511]==0xA5,"a written block reads back from memory (the image file is never touched)");
    // snapshot round trip: dirty blocks and state survive; a different image is refused
    std::vector<uint8_t> bytes;{snap::Ar a(bytes);d.serialize(a);}
    UsbMassStorage e(makeImage());{snap::Ar a(bytes.data(),bytes.size());e.serialize(a);}
    e.bulkOut(cbw(8,512,true,{0x28,0,0,0,0,7,0,0,1,0}));
    auto drainE=[&]{std::vector<uint8_t> all;for(int g=0;g<64;++g){auto p=e.bulkIn(512);if(p.empty())break;all.insert(all.end(),p.begin(),p.end());}return all;};
    r=drainE();
    require(r.size()==512+13 && r[0]==0xA5 && e.writtenBlocks==1,"snapshot restores written blocks and counters");
    auto other=makeImage();other[100]^=1;UsbMassStorage f(other);
    bool refused=false;try{snap::Ar a(bytes.data(),bytes.size());f.serialize(a);}catch(const snap::SnapshotError&){refused=true;}
    require(refused,"a snapshot is refused when the pendrive image differs");
    // control requests
    uint8_t s[8]={0x80,6,0,1,0,0,18,0};d.setup(s);
    auto dd=d.controlIn(64);
    require(dd.size()==18 && dd[0]==18 && dd[1]==1 && dd[7]==64,"GET_DESCRIPTOR(device) returns the 18-byte descriptor with ep0 max packet 64");
    uint8_t cfg[8]={0x80,6,0,2,0,0,255,0};d.setup(cfg);auto cd=d.controlIn(255);
    require(cd.size()==32 && cd[9+5]==8 && cd[9+6]==6 && cd[9+7]==0x50,"configuration descriptor holds a mass-storage / SCSI / bulk-only interface");
    uint8_t addr[8]={0,5,3,0,0,0,0,0};d.setup(addr);require(d.address()==0,"SET_ADDRESS is applied only after the status stage");d.controlStatus();
    require(d.address()==3,"SET_ADDRESS takes effect after the status stage");
    uint8_t bad[8]={0x80,0x77,0,0,0,0,0,0};d.setup(bad);require(d.takeStall(),"unknown standard request stalls");
  }
  // ---- EHCI-style controller over fake DRAM ----
  {
    std::vector<uint8_t> ram(0x10000,0);
    auto put32=[&](uint32_t a,uint32_t v){for(int i=0;i<4;++i)ram[a+i]=uint8_t(v>>(8*i));};
    auto get32=[&](uint32_t a){return uint32_t(ram[a]|ram[a+1]<<8|ram[a+2]<<16|uint32_t(ram[a+3])<<24);};
    UsbHost h;h.attach(makeImage());
    bool powered=false;uint32_t asyncBase=0;
    h.bus.read=[&](uint32_t a,size_t n){return std::vector<uint8_t>(ram.begin()+a,ram.begin()+a+n);};
    h.bus.write=[&](uint32_t a,std::span<const uint8_t> b){std::copy(b.begin(),b.end(),ram.begin()+a);};
    h.bus.event=[](const std::string&,uint32_t,uint32_t,const std::string&){};
    h.bus.phyPowered=[&](unsigned){return powered;};
    h.bus.storedKnown=[&](uint32_t){return true;};
    h.bus.storedReg=[&](uint32_t off){return off==0x28?asyncBase:0u;};
    auto addr=[](unsigned idx){return UsbHost::kBase+(idx&~1u)*2+(idx&1);};
    uint64_t t=1000;
    require((h.read(addr(0x30),2,t)&1)==0,"no connection before the guest powers the PHY");
    powered=true;
    require((h.read(addr(0x30),2,++t)&3)==3,"powered PHY: CCS and CSC set");
    h.write(addr(0x30),2,0x1100,++t);   // PR
    require((h.read(addr(0x30),2,++t)&0x100)!=0,"port reset in progress while PR is held");
    t+=30000;
    require((h.read(addr(0x30),2,++t)&0x105)==0x5 && h.read(addr(0x41),1,t)==0x04,"after the reset: PE set, PR clear, host speed 2 (high-speed)");
    // queue: GET_DESCRIPTOR(device) on address 0, endpoint 0
    const uint32_t QH=0x1000,Q1=0x2000,Q2=0x2040,Q3=0x2080,DUMMY=0x20c0,SETUP=0x3000,DATA=0x3100;
    put32(QH,QH|2);put32(QH+4,0|(0<<8)|(64u<<16)|(1u<<15)|(2u<<12));put32(QH+8,0);put32(QH+0x10,Q1);
    put32(Q1,Q2);put32(Q1+4,1);put32(Q1+8,0x80|(2u<<8)|(8u<<16));put32(Q1+12,SETUP);
    put32(Q2,Q3);put32(Q2+4,1);put32(Q2+8,0x80|(1u<<8)|(18u<<16)|(1u<<31)|0x8000);put32(Q2+12,DATA);
    put32(Q3,DUMMY);put32(Q3+4,1);put32(Q3+8,0x80|(0u<<8)|(0u<<16)|(1u<<31)|0x8000);
    put32(DUMMY,1);put32(DUMMY+4,1);put32(DUMMY+8,0);
    const uint8_t setup[8]={0x80,6,0,1,0,0,18,0};std::copy(setup,setup+8,ram.begin()+SETUP);
    asyncBase=QH|0xA0000000u;   // the original driver programs KSEG1 aliases; masking is done by the machine bus
    asyncBase=QH;
    h.write(addr(0x10),2,0x0021,++t);   // RUN | ASE
    t+=1000;h.advance(t,true);
    require((get32(Q1+8)&0x80)==0 && (get32(Q2+8)&0x80)==0 && (get32(Q3+8)&0x80)==0,"all three qTDs of the control transfer are retired");
    require(((get32(Q2+8)>>16)&0x7fff)==0 && ram[DATA]==18 && ram[DATA+1]==1 && ram[DATA+7]==64,"the IN data stage delivers the device descriptor into the guest buffer");
    require(get32(QH+0x10)==DUMMY,"QH.qtd_next is left pointing at the inactive dummy qTD (driver fills it in place)");
    require((h.read(addr(0x14),2,++t)&1)==1 && h.transactions==3,"IOC raises USBSTS.INT; three transactions were executed");
    h.write(addr(0x14),2,0x0001,++t);
    require((h.read(addr(0x14),2,++t)&1)==0,"USBSTS.INT is write-1-clear");
    // a transaction to a different address finds no device: transaction error, qTD halted
    put32(DUMMY,DUMMY+0x40);put32(DUMMY+4,1);put32(DUMMY+8,0x80|(1u<<8)|(8u<<16));put32(DUMMY+12,DATA);   // reuse the dummy as a new qTD
    put32(QH+4,5|(0<<8)|(64u<<16)|(1u<<15)|(2u<<12));
    t+=1000;h.advance(t,true);
    require((get32(DUMMY+8)&0x40)!=0 && (get32(DUMMY+8)&0x80)==0,"a transaction to an unassigned address halts the qTD");
    // snapshot round trip of the controller + device
    std::vector<uint8_t> bytes;{snap::Ar a(bytes);h.serialize(a);}
    UsbHost g;g.attach(makeImage());{snap::Ar a(bytes.data(),bytes.size());g.serialize(a);}
    require(g.transactions==h.transactions && g.cmd()==h.cmd() && g.status()==h.status(),"controller state survives a snapshot round trip");
  }
}
