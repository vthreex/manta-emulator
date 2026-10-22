void palCvbsTests(){
  using namespace manta::cvbs;
  PalSource src;
  const uint64_t frame=uint64_t(PalTiming::samplesPerLine)*PalTiming::linesPerFrame;
  // generator sanity: sync tip, blanking, burst and white bar levels at known positions (line 100, active picture)
  {
    auto lineStart=[&](unsigned line){return uint64_t(line-1)*PalTiming::samplesPerLine;};
    const double tip=src.voltsAt(lineStart(100)+20);
    require(std::fabs(tip+0.3)<1e-9,"PAL sync tip is -300 mV");
    const double blank=src.voltsAt(lineStart(100)+250);                       // 9.3 us, between burst and active video
    require(std::fabs(blank)<1e-9,"PAL blanking level is 0 V");
    double mn=1,mx=-1;
    for(unsigned x=int(5.7e-6*27e6);x<int(7.7e-6*27e6);++x){double v=src.voltsAt(lineStart(100)+x);mn=std::min(mn,v);mx=std::max(mx,v);}
    require(mx-mn>0.28&&mx-mn<0.31,"PAL burst is 300 mV peak to peak");
  }
  // measurement of a clean PAL signal over two frames (a slice that starts off a line boundary)
  {
    const auto m=measure(src,12345,2*frame);
    require(m.hsyncLocked,"clean PAL: horizontal sync locks");
    require(m.lineClocks==PalTiming::samplesPerLine,"clean PAL: 1728 clocks per line (15.625 kHz at 27 MHz)");
    require(m.hsyncWidthClocks>=125&&m.hsyncWidthClocks<=129,"clean PAL: line sync is 4.7 us (about 127 clocks)");
    require(std::fabs(m.syncAmplitudeVolts-0.3)<0.01,"clean PAL: sync amplitude 300 mV");
    require(m.vsyncSeen&&m.linesPerFrame==625,"clean PAL: 625 lines per frame");
    require(m.linesPerField==312||m.linesPerField==313,"clean PAL: 312.5 lines per field");
    require(m.interlaced,"clean PAL: 2:1 interlace (vertical sync alternates by half a line)");
    require(m.burstPresent&&std::fabs(m.burstPeakToPeakVolts-0.3)<0.03,"clean PAL: colour burst 300 mV p-p");
    require(m.palSwitch,"clean PAL: burst phase alternates by 90 degrees line to line (PAL)");
  }
  // noise does not break lock
  {
    PalSource n;n.noiseVolts=0.02;
    const auto m=measure(n,777,2*frame);
    require(m.hsyncLocked&&m.lineClocks==PalTiming::samplesPerLine&&m.vsyncSeen&&m.linesPerFrame==625,"PAL with 20 mV noise still locks");
  }
  // no colour: monochrome signal has sync but no burst
  {
    PalSource mono;mono.burst=false;
    const auto m=measure(mono,0,2*frame);
    require(m.hsyncLocked&&m.vsyncSeen&&!m.burstPresent&&!m.palSwitch,"monochrome PAL: sync present, no burst, no PAL switch");
  }
  // open input: nothing locks
  {
    PalSource open;open.sync=false;open.noiseVolts=0.01;
    const auto m=measure(open,0,frame);
    require(!m.hsyncLocked&&!m.vsyncSeen&&!m.burstPresent,"open input: no sync, no burst");
  }
}
void palDecodeTests(){
  using namespace manta::cvbs;
  PalSource src;
  const auto f=decodePalFrame(src,0);
  require(f.width==720&&f.height==576&&f.bgra.size()==720*576,"decoded PAL frame is 720x576");
  // 8 colour bars across the active picture; sample the centre of each bar on a mid picture row
  auto px=[&](unsigned bar){const unsigned x=unsigned((bar+0.5)*720/8);return f.bgra[size_t(300)*720+x];};
  auto ch=[](uint32_t p,int shift){return int((p>>shift)&255);};
  struct Exp{int r,g,b;};
  // EBU 100/0/75/0 bars, gamma-corrected values; tolerance generous: simple PAL decoder with box filters on a 27 MHz grid
  const Exp want[8]={{255,255,255},{191,191,0},{0,191,191},{0,191,0},{191,0,191},{191,0,0},{0,0,191},{0,0,0}};
  for(unsigned b=0;b<8;++b){
    const auto p=px(b);
    const int dr=std::abs(ch(p,16)-want[b].r),dg=std::abs(ch(p,8)-want[b].g),db=std::abs(ch(p,0)-want[b].b);
    require(dr<40&&dg<40&&db<40,"decoded colour bar matches the EBU bar within tolerance");
  }
}
