#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace manta {
// Decode-only description of a stretched GE BitBlt (command 8040 / C040).
// It validates that the programmed registers are internally consistent with
// the original firmware's own scale arithmetic and records which hardware
// semantics are still unestablished. It never reads or writes guest memory
// and cannot be queued: Titania4 filtering, dithering, key comparison and the
// format-C alpha conversion remain unproved, so no raster effect is offered.
struct GeStretchRequest {
  using Register=std::function<std::pair<uint16_t,uint16_t>(unsigned)>; // value, known
  bool nearest=false;
  uint16_t control=0,mode=0,alpha11=0,alpha12=0,alpha13=0;
  unsigned sourceFormat=0,destinationFormat=0;
  uint32_t source=0,destination=0,sourcePitch=0,destinationPitch=0;
  uint32_t ratioX=0,ratioY=0;int phaseX=0,phaseY=0;bool phaseUpX=false,phaseUpY=false;
  uint32_t destLeft=0,destTop=0,destRight=0,destBottom=0,srcX=0,srcY=0,srcWidth=0,srcHeight=0;
  uint32_t clipLeft=0,clipRight=0,clipTop=0,clipBottom=0;
  uint32_t keyLow=0,keyHigh=0;bool keyModeBit=false;
  bool dither()const{return control&0x02;}
  bool blend()const{return control&0x04;}
  bool sourceKey()const{return control&0x40;}
  // Original software result (80526BA0 packer, documented in
  // ge-bitblt-copy-and-scale.md): ratio = floor(src*4096/dst) when src<32*dst.
  static uint32_t expectedRatio(uint32_t src,uint32_t dst){
    return src<32ull*dst?uint32_t(uint64_t(src)*4096/dst):0xffffffffu;}
  static uint32_t expectedPhase(uint32_t src,uint32_t dst){
    int64_t q=(int64_t(src)-int64_t(dst))*4096/int64_t(2*dst); // C++ truncates toward zero
    uint32_t p=uint32_t(q)&0xfff;if(src<dst)p|=0x1000;return p;}
  // What the available evidence does NOT establish for this request.
  std::vector<std::string> unresolved()const{
    std::vector<std::string> r;
    r.push_back("hardware sampling position/rounding of the fixed-point ratio and phase");
    if(!nearest)r.push_back("non-nearest filter taps and edge handling");
    if(dither())r.push_back("dither pattern, coordinate dependence and enable semantics for this format pair");
    if(blend())r.push_back("blend equation, alpha rounding and ordering against stretch");
    if(sourceKey())r.push_back("key comparator applied before or after stretch filtering, and key range inclusivity");
    if(destinationFormat==0x0c)r.push_back("format-C alpha-bit quantization and whether source alpha or register 11 supplies it");
    if(sourceFormat==0x09)r.push_back("source ARGB1555 alpha expansion when bit 15 is clear");
    return r;}
  // Human-readable decode for the stop reason; read-only, no raster effect.
  std::string describe()const{
    auto hx=[](uint64_t v){char b[24];snprintf(b,sizeof b,"0x%llX",(unsigned long long)v);return std::string(b);};
    std::string t="decoded request (decode-only, not executed): cmd="+std::string(nearest?"C040":"8040")+" ctl="+hx(control)+" mode="+hx(mode)
      +" fmt src="+hx(sourceFormat)+" dst="+hx(destinationFormat)+" src="+hx(source)+"/pitch "+std::to_string(sourcePitch)
      +" dst="+hx(destination)+"/pitch "+std::to_string(destinationPitch)
      +" destRect=("+std::to_string(destLeft)+","+std::to_string(destTop)+")-("+std::to_string(destRight)+","+std::to_string(destBottom)+")"
      +" srcRect=("+std::to_string(srcX)+","+std::to_string(srcY)+") "+std::to_string(srcWidth)+"x"+std::to_string(srcHeight)
      +" ratio="+hx(ratioX)+"/"+hx(ratioY)+" dither="+(dither()?"1":"0")+" blend="+(blend()?"1":"0")+" key="+(sourceKey()?"1":"0")
      +" alpha11..13="+hx(alpha11)+","+hx(alpha12)+","+hx(alpha13)+"; unresolved:";
    for(auto& u:unresolved())t+=" ["+u+"]";
    return t;}
  static GeStretchRequest decode(uint16_t command,const Register& reg){
    if(command!=0x8040 && command!=0xc040)throw std::runtime_error("not a stretched GE BitBlt command");
    auto field=[&](unsigned index,uint16_t mask=0xffff){auto [value,known]=reg(index);
      if((known&mask)!=mask)throw std::runtime_error("GE stretch field contains unknown bits");
      return uint32_t(value&mask);};
    GeStretchRequest p;p.nearest=command==0xc040;
    p.control=uint16_t(field(0));
    if(!(p.control&1))throw std::runtime_error("GE stretch requires enabled engine");
    p.mode=uint16_t(field(1,0x372));
    // Original 8051F9E8-style setup and Marlon BitBltEX both set mode bit 4
    // together with the 8000 command bit; a mismatch means the request is not
    // the corroborated stretch configuration.
    if(!(p.mode&0x10))throw std::runtime_error("GE stretch command without stretch-enable mode bit");
    // Register 59 is zero in every corroborated plain and stretched request;
    // its non-zero meaning (orientation/mirror) is not modeled here.
    // Bits 1:0 are the orientation field (unmodeled); 0x40 is the GE-init default (805267AC) present in connected runs.
    if((field(0x59)&~0x40u)!=0)throw std::runtime_error("GE stretch orientation register non-zero; unsupported");
    auto format=field(0x34,0x1f1f);p.sourceFormat=format&0x1f;p.destinationFormat=format>>8;
    p.source=field(0x20)|(field(0x21)<<16);p.destination=field(0x26)|(field(0x27)<<16);
    p.sourcePitch=field(0x30);p.destinationPitch=field(0x33);
    p.destLeft=field(0x68);p.destTop=field(0x69);p.destRight=field(0x6a);p.destBottom=field(0x6b);
    p.srcX=field(0x6c);p.srcY=field(0x6d);p.srcWidth=field(0x6e);p.srcHeight=field(0x6f);
    p.clipLeft=field(0x55);p.clipRight=field(0x56);p.clipTop=field(0x57);p.clipBottom=field(0x58);
    if(p.destLeft>p.destRight || p.destTop>p.destBottom || !p.srcWidth || !p.srcHeight)
      throw std::runtime_error("GE stretch rectangle invalid");
    auto dw=p.destRight-p.destLeft+1,dh=p.destBottom-p.destTop+1;
    uint32_t x=field(0x64),y=field(0x65),px=field(0x5e),py=field(0x5f);
    p.ratioX=x|((px>>15&1)<<16);p.ratioY=y|((py>>15&1)<<16);
    p.phaseUpX=px&0x1000;p.phaseUpY=py&0x1000;
    int sx=int(px&0xfff),sy=int(py&0xfff);
    p.phaseX=sx&0x800?sx-0x1000:sx;p.phaseY=sy&0x800?sy-0x1000:sy;
    // Cross-check against the original arithmetic. Disagreement would mean
    // the registers were not produced by the corroborated scale routine.
    if(p.ratioX!=(expectedRatio(p.srcWidth,dw)&0x1ffff) || p.ratioY!=(expectedRatio(p.srcHeight,dh)&0x1ffff))
      throw std::runtime_error("GE stretch ratio disagrees with original scale arithmetic");
    // 5E/5F bit 15 carries the ratio extension, so only bits 0..12 are phase.
    if((px&0x1fff)!=expectedPhase(p.srcWidth,dw))
      throw std::runtime_error("GE stretch horizontal phase disagrees with original arithmetic");
    // With the software-context flag and source height below six, the
    // original routine substitutes vertical phase 1000 instead.
    if((py&0x1fff)!=expectedPhase(p.srcHeight,dh) && !(p.srcHeight<6 && (py&0x1fff)==0x1000))
      throw std::runtime_error("GE stretch vertical phase disagrees with original arithmetic");
    p.alpha11=uint16_t(field(0x11));p.alpha12=uint16_t(field(0x12));p.alpha13=uint16_t(field(0x13));
    if(p.sourceKey()){
      p.keyLow=field(0x16)|(field(0x17)<<16);p.keyHigh=field(0x14)|(field(0x15)<<16);
      p.keyModeBit=field(0x1c,1)&1;}
    return p;}
};
}
