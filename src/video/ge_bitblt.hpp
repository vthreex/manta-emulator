#pragma once
#include "video/ge_rect_fill.hpp"
namespace manta {
// The unscaled 0040 path is corroborated by original 80523BFC instructions.
// Deliberately excludes format C, conversion, alpha, keys and stretch: those
// require more evidence than the shared register layout alone provides.
struct GeBitBlt {
  uint32_t source=0,destination=0,sourcePitch=0,pitch=0;
  uint32_t sourceX=0,sourceY=0,left=0,top=0,right=0,bottom=0;
  unsigned bpp=0;bool empty=false;
  // yuvRelax (EXPLORATORY, opt-in with --exploratory-ge-yuv-stretch): also accepts source=destination format 0xE (packed 16-bit, copied byte for byte), register 59
  // equal to the GE-init default 0x0040 (as the fill model does), and a programmed source height 5 rows short of the destination rectangle (the original library's
  // "height - 5" programming, hypothesis H5: the hardware processes the full destination rectangle).
  static GeBitBlt decode(uint16_t command,const GeRectFill::Register& reg,bool yuvRelax=false){
    if(command!=0x40)throw std::runtime_error("GE BitBlt stretch/filter/mode unsupported");
    auto field=[&](unsigned index,uint16_t mask=0xffff){auto [value,known]=reg(index);
      if((known&mask)!=mask)throw std::runtime_error("GE BitBlt field contains unknown bits");
      return uint32_t(value&mask);};
    if(field(0)!=1 || field(1,0x372)!=0 || (field(0x59)!=0 && !(yuvRelax && field(0x59)==0x40)))
      throw std::runtime_error("GE BitBlt requires enabled plain copy; transforms/blending/keying unsupported");
    auto format=field(0x34,0x1f1f);GeBitBlt p;
    if(format==0x0808)p.bpp=2;else if(format==0x0f0f)p.bpp=4;
    else if(yuvRelax && format==0x0e0e)p.bpp=2;
    else throw std::runtime_error("GE BitBlt format/conversion unsupported; no format-C alpha assumption");
    p.source=field(0x20)|(field(0x21)<<16);
    p.destination=field(0x26)|(field(0x27)<<16);
    p.sourcePitch=field(0x30);p.pitch=field(0x33);
    if(!p.pitch || !p.sourcePitch || p.pitch%p.bpp || p.sourcePitch%p.bpp)
      throw std::runtime_error("GE BitBlt pitch invalid");
    auto x0=field(0x68),y0=field(0x69),x1=field(0x6a),y1=field(0x6b);
    auto sx=field(0x6c),sy=field(0x6d),sw=field(0x6e),sh=field(0x6f);
    auto cl=field(0x55),cr=field(0x56),ct=field(0x57),cb=field(0x58);
    for(auto v:{x0,y0,x1,y1,sx,sy,sw,sh,cl,cr,ct,cb})
      if(v>4095)throw std::runtime_error("GE BitBlt coordinate outside corroborated field");
    if(yuvRelax && y1>=y0 && sh+5==y1-y0+1)sh+=5;   // H5
    if(x0>x1 || y0>y1 || sw!=x1-x0+1 || sh!=y1-y0+1 || !sw || !sh)
      throw std::runtime_error("GE BitBlt unequal/invalid unscaled extents");
    p.left=std::max(x0,cl);p.right=std::min(x1,cr);
    p.top=std::max(y0,ct);p.bottom=std::min(y1,cb);
    p.empty=cl>cr || ct>cb || p.left>p.right || p.top>p.bottom;
    if(p.source>=0x08000000 || p.destination>=0x08000000)
      throw std::runtime_error("GE BitBlt requires MIU0 byte addressing");
    if(p.empty)return p;
    p.sourceX=sx+p.left-x0;p.sourceY=sy+p.top-y0;
    auto width=p.right-p.left+1,height=p.bottom-p.top+1;
    if(p.sourceX+width>4096 || p.sourceY+height>4096)
      throw std::runtime_error("GE BitBlt source endpoint outside corroborated coordinate range");
    if(uint64_t(p.right+1)*p.bpp>p.pitch || uint64_t(p.sourceX+width)*p.bpp>p.sourcePitch)
      throw std::runtime_error("GE BitBlt exceeds programmed pitch");
    uint64_t ss=uint64_t(p.source)+uint64_t(p.sourceY)*p.sourcePitch+p.sourceX*p.bpp;
    uint64_t se=ss+uint64_t(height-1)*p.sourcePitch+width*p.bpp;
    uint64_t ds=uint64_t(p.destination)+uint64_t(p.top)*p.pitch+p.left*p.bpp;
    uint64_t de=ds+uint64_t(height-1)*p.pitch+width*p.bpp;
    if(se>0x08000000 || de>0x08000000)
      throw std::runtime_error("GE BitBlt range overflows MIU0");
    // Direction/overlap command bits are not established. Reject even possible
    // overlap across pitch padding rather than inventing memmove semantics.
    if(ss<de && ds<se)throw std::runtime_error("GE BitBlt overlapping ranges unsupported");
    return p;
  }
  void execute(const std::function<std::vector<uint8_t>(uint32_t,size_t)>& read,
               const std::function<bool(uint32_t,size_t)>& writable,
               const std::function<void(uint32_t,std::span<const uint8_t>)>& write)const{
    if(empty)return;
    auto width=(right-left+1)*bpp,height=bottom-top+1;
    auto start=destination+top*pitch+left*bpp;
    if(!read || !writable || !write || !writable(start,size_t(height-1)*pitch+width))
      throw std::runtime_error("GE BitBlt guest backing unavailable; no completion");
    // Read every source row before modifying the target. The machine's reader
    // rejects hardware-origin unknown bytes. A late invalid source therefore
    // cannot leave a partially painted framebuffer or a completion tag.
    std::vector<std::vector<uint8_t>> rows;rows.reserve(height);
    for(uint32_t y=0;y<height;++y){
      auto bytes=read(source+(sourceY+y)*sourcePitch+sourceX*bpp,width);
      if(bytes.size()!=width)throw std::runtime_error("GE BitBlt short source read; no completion");
      rows.push_back(std::move(bytes));
    }
    for(uint32_t y=0;y<height;++y)write(start+y*pitch,rows[y]);
  }
};
}
