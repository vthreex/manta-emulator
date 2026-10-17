#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
namespace manta {
// Narrow register-driven rectangle fill. No reset values, queue readiness,
// analog inputs or UI assets are supplied here. Original constructor command
// 0030 and endpoint/color registers corroborate the related GE register map.
// Blending, keying, gradients, format C/9 and MIU1 remain unsupported.
struct GeRectFill {
  uint32_t destination=0,pitch=0,left=0,top=0,right=0,bottom=0,color=0;
  unsigned bpp=0;bool empty=false;
  using Register=std::function<std::pair<uint16_t,uint16_t>(unsigned)>; // value, known
  static GeRectFill decode(uint16_t command,const Register& reg){
    if(command!=0x30)throw std::runtime_error("GE raster primitive/mode unsupported");
    auto field=[&](unsigned index,uint16_t mask=0xffff){auto [value,known]=reg(index);
      if((known&mask)!=mask)throw std::runtime_error("GE rectangle field contains unknown bits");
      return uint32_t(value&mask);};
    if(field(0)!=1 || field(1,0x372)!=0 || field(0x59)!=0)
      throw std::runtime_error("GE rectangle requires enabled plain fill; transforms/blending/keying unsupported");
    auto format=field(0x34,0x1f00)>>8;GeRectFill p;
    if(format==0xf)p.bpp=4;else if(format==8)p.bpp=2;
    else throw std::runtime_error("GE rectangle destination format unsupported; no format-C alpha assumption");
    p.destination=field(0x26)|(field(0x27)<<16);
    if(p.destination>=0x08000000)throw std::runtime_error("GE rectangle requires mapped MIU0 byte addressing");
    p.pitch=field(0x33);
    p.color=field(0x70)|(field(0x71)<<16);
    auto x0=field(0x68),y0=field(0x69),x1=field(0x6a),y1=field(0x6b);
    auto clipLeft=field(0x55),clipRight=field(0x56),clipTop=field(0x57),clipBottom=field(0x58);
    for(auto coordinate:{x0,y0,x1,y1,clipLeft,clipRight,clipTop,clipBottom})
      if(coordinate>4095)throw std::runtime_error("GE rectangle coordinate outside corroborated field");
    if(!p.pitch || p.pitch%p.bpp)throw std::runtime_error("GE rectangle pitch invalid");
    p.left=std::max(x0,clipLeft);p.right=std::min(x1,clipRight);
    p.top=std::max(y0,clipTop);p.bottom=std::min(y1,clipBottom);
    p.empty=x0>x1 || y0>y1 || clipLeft>clipRight || clipTop>clipBottom || p.left>p.right || p.top>p.bottom;
    if(!p.empty && uint64_t(p.right+1)*p.bpp>p.pitch)
      throw std::runtime_error("GE rectangle exceeds programmed destination pitch");
    if(!p.empty && uint64_t(p.destination)+uint64_t(p.bottom)*p.pitch+uint64_t(p.right+1)*p.bpp>0x08000000)
      throw std::runtime_error("GE rectangle destination overflows MIU0");
    return p;
  }
  void execute(const std::function<bool(uint32_t,size_t)>& writable,
               const std::function<void(uint32_t,std::span<const uint8_t>)>& write)const{
    if(empty)return;
    auto start=destination+top*pitch+left*bpp;
    auto width=(right-left+1)*bpp;
    auto extent=size_t(bottom-top)*pitch+width;
    if(!writable || !write || !writable(start,extent))
      throw std::runtime_error("GE rectangle destination backing unavailable; no completion");
    // Validate the complete range before the first write. Padding and rows
    // outside the clipped rectangle are never overwritten.
    uint32_t pixel=bpp==4?color:((color>>8)&0xf800)|((color>>5)&0x7e0)|((color>>3)&0x1f);
    std::vector<uint8_t> row(width);
    for(size_t i=0;i<row.size();++i)row[i]=uint8_t(pixel>>(8*(i%bpp)));
    for(uint32_t y=top;y<=bottom;++y)write(destination+y*pitch+left*bpp,row);
  }
};
}
