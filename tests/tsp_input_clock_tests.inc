void tspInputClockTests(){
  manta::TspTransport input;input.packetCycles=100;
  input.regRead=[](uint32_t a)->manta::OrWord{return {a==0x210000?0x00200000u:0x1fffu,~0u,{}};};
  std::vector<uint8_t> bytes(3*188);
  for(unsigned i=0;i<3;++i){bytes[188*i]=0x47;bytes[188*i+3]=0x10;bytes[188*i+5]=uint8_t(10+i);}
  require(input.clockedSupply(0,bytes,1000) && input.inputPackets==1,"a timed external arrival delivers actual bytes at its configured boundary");
  for(unsigned i=0;i<10;++i)input.clockedSupply(0,bytes,1099);
  require(input.inputPackets==1,"repeated access before the next arrival cannot generate transport packets");
  auto release=[&]{input.write(0x210218,{0x10,~0u,{}});input.write(0x210218,{0,~0u,{}});};
  release();require(!input.clockedSupply(0,bytes,1099),"freeing a slot cannot invent an early transmitter arrival");
  require(input.clockedSupply(0,bytes,1100) && input.inputPackets==2,"the next packet arrives at its own deadline");
  input.clockedSupply(0,bytes,1400);
  require(input.droppedPackets==3 && input.inputPackets==5 && input.nextPacketTick==1500,"a busy one-slot input drops elapsed arrivals instead of pausing the transmitter");
  release();require(input.clockedSupply(0,bytes,1500),"reception resumes on the real next arrival");input.write(0x210200,{5,~0u,{}});
  require((input.read(0x210204)->value&255)==12,"lost arrivals advance the source position rather than replaying a stale packet");
  rejects([&]{input.clockedSupply(0,bytes,1499);});
}
void tspPacedIdleEquivalenceTests(){
  auto run=[](bool skip){
    manta::Riu r;r.event=[](auto&&,auto,auto,auto,auto&&){};r.symbolicInit=true;r.candidateTsp=true;
    r.configureExploratoryTspResetLow();r.configureExploratoryTspCpu();r.configureOrTraceBudget(true,100000,4);r.orIdleSkip=skip;
    std::vector<uint8_t> code(0x200);const uint32_t words[]={0x18000000,0x9c200123,0,0x15000000};
    for(unsigned i=0;i<4;++i)for(unsigned b=0;b<4;++b)code[0x100+4*i+b]=uint8_t(words[i]>>(8*b));
    r.dramRead=[&](uint32_t,size_t){return code;};
    r.candidateTspPackets=true;r.bindTspTransport();r.tspTransport.packetCycles=100;
    for(unsigned i=0;i<48;++i)r.tspTransport.regWrite(0x210000+4*i,i?0x1fff:0x00200000);
    r.pllTuner(true);r.tuner().lastCount=5;const uint8_t pll[]={0x10,0xd5,0x90,0x14,0xc0};std::copy(std::begin(pll),std::end(pll),r.tuner().last);
    manta::DvbtRf::Station station;station.mhz=682;station.bandwidthMHz=8;station.transport.resize(188);station.transport[0]=0x47;station.transport[3]=0x10;
    r.dvbtRf.enabled=true;r.dvbtRf.stations.push_back(station);
    uint64_t tick=0;auto w=[&](unsigned off,unsigned v){r.write(0x1f202b00+off,2,v,0,65535,tick);};
    w(0xe8,0);w(0xe8,2);for(unsigned off:{0x88,0x8c,0x98,0x9c})w(off,0);
    for(unsigned off:{0x90,0xa0})w(off,0xc000);for(unsigned off:{0x94,0xa4})w(off,0xffff);
    w(0xe0,0xcb6c);w(0xe4,unsigned(code.size()/4));r.write(0x1f202c1c,2,1,0,65535,tick);
    tick=100;w(0xe8,6);tick=132;r.read(0x1f202be8,2,0,8,tick);tick=133;w(0xe8,3);
    r.read(0x1f202be8,2,0,1,20133);r.read(0x1f202be8,2,0,1,20134);
    require(r.tspTransport.inputPackets==201 && r.tspTransport.droppedPackets==200 && r.tspTransport.delivered()==1,"every elapsed transmitter slot is accounted for while an isolated real CPU idle loop executes");
    std::vector<uint8_t> state;manta::snap::Ar save(state);r.tspTransport.serialize(save);
    return std::pair{r.tspCoreStateSha(),state};
  };
  require(run(false)==run(true),"TSP idle fast-forward cannot skip a packet deadline or change CPU, packet, drop or clock state");
}
