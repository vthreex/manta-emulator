#pragma once
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace manta {
// EXPLORATORY ideal DVB-T input, not an OFDM/ADC or calibrated acquisition
// model. A station supplies actual post-FEC transport bytes and explicit RF
// bandwidth. Only named silicon observations are derived here. Software-owned
// TOP2070, FEC23E0, mailbox replies, sections and TV channels are never supplied.
class DvbtRf {
public:
  struct Station { double mhz=0;unsigned bandwidthMHz=8;std::string file;std::vector<uint8_t> transport; };
  struct Observation { uint8_t value=0,known=0,owned=0; };
  bool enabled=false;
  std::vector<Station> stations;
  static void validate(const Station& s){
    if(!std::isfinite(s.mhz) || s.mhz<=0 || s.mhz>2000 || s.bandwidthMHz<6 || s.bandwidthMHz>8)
      throw std::runtime_error("DVB-T station requires a finite RF MHz value and bandwidth6/7/8");
    if(s.transport.empty() || s.transport.size()>64*1024*1024 || s.transport.size()%188)
      throw std::runtime_error("DVB-T input requires188-byte transport packets,1..64MiB");
    for(size_t i=0;i<s.transport.size();i+=188)if(s.transport[i]!=0x47)
      throw std::runtime_error("DVB-T input loses188-byte transport synchronization");
  }
  // Windows filenames cannot contain '|'. No service metadata is parsed here.
  // off describes an explicitly disconnected ideal input; it supplies no TS.
  static DvbtRf load(const std::string& text){
    DvbtRf r;r.enabled=true;if(text=="off")return r;
    size_t start=0;
    while(start<text.size()){
      const auto end=text.find(';',start);const auto item=text.substr(start,end==std::string::npos?end:end-start);
      const auto a=item.find('|'),b=a==std::string::npos?a:item.find('|',a+1);
      if(a==std::string::npos || b==std::string::npos || b+1==item.size())
        throw std::runtime_error("DVB-T input syntax: MHz|bandwidth|FILE.ts[;...], or off");
      size_t n=0;Station s;s.mhz=std::stod(item.substr(0,a),&n);
      if(n!=a)throw std::runtime_error("DVB-T RF frequency has trailing characters");
      const auto bw=item.substr(a+1,b-a-1);s.bandwidthMHz=std::stoul(bw,&n);
      if(n!=bw.size())throw std::runtime_error("DVB-T bandwidth has trailing characters");
      s.file=item.substr(b+1);
      const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.file.data()),s.file.size()));
      std::ifstream f(path,std::ios::binary|std::ios::ate);
      if(!f || f.tellg()<=0 || f.tellg()>64*1024*1024)throw std::runtime_error("cannot read bounded DVB-T transport input");
      s.transport.resize(size_t(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char*>(s.transport.data()),s.transport.size());
      if(!f)throw std::runtime_error("incomplete DVB-T transport input read");
      validate(s);r.stations.push_back(std::move(s));if(end==std::string::npos)break;start=end+1;
    }
    if(r.stations.empty())throw std::runtime_error("empty DVB-T station specification");
    return r;
  }
  // Half the observed166.667-kHz raster is the explicit ideal capture window;
  // this is a model assumption, not a measured tuner acquisition curve.
  std::optional<bool> reception(double tunedMHz,unsigned tunedBandwidth)const{
    if(!enabled)return {};
    const Station* candidate=nullptr;
    for(const auto& s:stations)if(tunedMHz>=0 && std::abs(tunedMHz-s.mhz)<=1.0/12.0){
      if(candidate)return {}; // cochannel superposition/phase has no model yet
      candidate=&s;
    }
    if(!candidate)return false;
    if(!tunedBandwidth)return {}; // six/seven MHz cannot be inferred from the shared tuner bit
    return tunedBandwidth==candidate->bandwidthMHz;
  }
  Observation sample(uint16_t xdata,double tunedMHz,unsigned tunedBandwidth)const{
    if(!enabled)return {};
    const auto lock=reception(tunedMHz,tunedBandwidth);
    uint8_t bit=0;
    switch(xdata){
      case 0x212f:case 0x2145:bit=1;break; // AGC / DCR: related T4 driver lock readers
      case 0x24cf:bit=0x10;break;         // mode/CP detection
      case 0x2222:case 0x232c:bit=2;break;// TPS / post-FEC packet synchronization
      default:break;
    }
    if(bit)return {uint8_t(lock.value_or(false)?bit:0),uint8_t(lock.has_value()?bit:0),bit};
    // Transmitter parameters:64QAM, no hierarchy, HP/LP2/3,8K,GI1/32.
    // They are silicon TPS data, not the software-published TOP status byte.
    const uint8_t mask=xdata==0x2224?0x73:xdata==0x2225?0x77:xdata==0x2226?0x33:0;
    const uint8_t value=xdata==0x2224?2:xdata==0x2225?0x11:xdata==0x2226?0x10:0;
    return {value,uint8_t(lock.value_or(false)?mask:0),mask};
  }
};
}
