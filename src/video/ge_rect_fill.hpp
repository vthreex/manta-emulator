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
  unsigned bpp=0;bool empty=false;bool argb1555=false;bool indexed=false;
  using Register=std::function<std::pair<uint16_t,uint16_t>(unsigned)>; // value, known
  // i8Hypothesis: EXPLORATORY. Destination format 4 is 8 bits per pixel according to the original format table (80525E28: fmt 4 -> 1 byte/pixel;
  // 6..14 -> 2, 15 -> 4), but how a fill colour maps to an index is NOT established; the hypothesis writes the low byte of registers 70/71.
  static GeRectFill decode(uint16_t command,const Register& reg,bool i8Hypothesis=false){
    if(command!=0x30)throw std::runtime_error("GE raster primitive/mode unsupported");
    auto field=[&](unsigned index,uint16_t mask=0xffff){auto [value,known]=reg(index);
      if((known&mask)!=mask)throw std::runtime_error("GE rectangle field contains unknown bits");
      return uint32_t(value&mask);};
    // Register 59: bits 1:0 are the orientation field and must be clear. Bit 6 is the value the original GE init
    // (805267AC) writes as the reset state; the reference fill driver never consults register 59, so bit 6 alone is
    // accepted for fills only (its hardware meaning is unverified). Any other bit still rejects.
    if(field(0)!=1 || field(1,0x372)!=0 || (field(0x59)&~0x40u)!=0)
      throw std::runtime_error("GE rectangle requires enabled plain fill; transforms/blending/keying unsupported");
    auto format=field(0x34,0x1f00)>>8;GeRectFill p;
    if(format==0xf)p.bpp=4;else if(format==8)p.bpp=2;
    else if(format==0xc){
      // ARGB1555_DST (destination-only 16 bit). Alpha quantization and channel order are not established, so only colors
      // whose result cannot depend on them are accepted: alpha 00 or FF and R=G=B in {00,FF} (black/white), which give
      // 8000/FFFF/0000/7FFF for every monotonic alpha rule and any RGB order, assuming A:1 R:5 G:5 B:5 with A in bit 15.
      p.bpp=2;p.argb1555=true;
    }
    else if(format==4 && i8Hypothesis){p.bpp=1;p.indexed=true;}
    else throw std::runtime_error("GE rectangle destination format unsupported; no format-C alpha assumption");
    p.destination=field(0x26)|(field(0x27)<<16);
    if(p.destination>=0x08000000)throw std::runtime_error("GE rectangle requires mapped MIU0 byte addressing");
    p.pitch=field(0x33);
    p.color=field(0x70)|(field(0x71)<<16);
    if(p.argb1555){
      uint32_t a=p.color>>24,r=(p.color>>16)&255,g=(p.color>>8)&255,b=p.color&255;
      if((a!=0 && a!=255) || r!=g || g!=b || (r!=0 && r!=255))
        throw std::runtime_error("GE format-C fill colour outside the rule-independent subset (alpha 00/FF, black or white)");
    }
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
    uint32_t pixel=indexed?(color&0xff):bpp==4?color:argb1555?(((color>>24)==255?0x8000u:0u)|(((color&255)==255)?0x7fffu:0u))
                                       :((color>>8)&0xf800)|((color>>5)&0x7e0)|((color>>3)&0x1f);
    std::vector<uint8_t> row(width);
    for(size_t i=0;i<row.size();++i)row[i]=uint8_t(pixel>>(8*(i%bpp)));
    for(uint32_t y=top;y<=bottom;++y)write(destination+y*pitch+left*bpp,row);
  }
};
}
