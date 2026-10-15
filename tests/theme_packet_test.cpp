#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <iterator>
#include "theme_packet.h"
#include "api_origin.h"
struct Reader {
  const std::vector<uint8_t>& bytes;size_t pos=0;
  explicit Reader(const std::vector<uint8_t>& input):bytes(input) {}
  int read(uint8_t* dst,size_t n) { size_t left=bytes.size()-pos;n=n<left?n:left;memcpy(dst,bytes.data()+pos,n);pos+=n;return n; }
};
void put16(std::vector<uint8_t>& p,unsigned at,uint16_t v) { p[at]=v&255;p[at+1]=v>>8; }
void put32(std::vector<uint8_t>& p,unsigned at,uint32_t v) { for (int i=0;i<4;++i)p[at+i]=(v>>(i*8))&255; }
void checksums(std::vector<uint8_t>& p) {
  put32(p,32,themeCrc(0xFFFFFFFF,p.data()+40,p.size()-40)^0xFFFFFFFF);
  put32(p,36,themeCrc(0xFFFFFFFF,p.data(),36)^0xFFFFFFFF);
}
const char* check(const std::vector<uint8_t>& p,unsigned size=0) { Reader r{p};ThemeHeader h;return validateThemePacket(r,size?size:p.size(),h,[](){}); }
std::vector<uint8_t> fixture() {
  std::vector<uint8_t> p(40+11*6*12+4,0);memcpy(p.data(),"NPTF",4);p[4]=1;p[5]=1;
  put16(p,6,240);put16(p,8,240);put16(p,10,198);put16(p,12,224);p[14]=6;p[15]=12;
  put16(p,18,0x9CF3);put16(p,20,0xF924);put32(p,24,11*6*12);put32(p,28,4);
  for(unsigned glyph=0;glyph<11;++glyph)p[40+glyph*6*12]=255;
  put16(p,40+11*6*12,57600);checksums(p);return p;
}
int main(int argc,char**argv) {
  if (argc==2) {
    std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> p((std::istreambuf_iterator<char>(file)),{});
    const char* error=check(p);if(error){std::cerr<<error<<'\n';return 1;}
    std::cout<<"valid "<<p.size()<<'\n';return 0;
  }
  auto p=fixture();assert(!check(p));
  auto invisible=p;invisible[40]=0;checksums(invisible);assert(!strcmp(check(invisible),"frame_glyphs"));
  auto sameColors=p;put16(sameColors,20,0x9CF3);checksums(sameColors);assert(!strcmp(check(sameColors),"frame_status_colors"));
  auto headerStatus=p;put16(headerStatus,10,205);put16(headerStatus,12,5);checksums(headerStatus);assert(!check(headerStatus));
  auto receiptStatus=p;put16(receiptStatus,10,187);put16(receiptStatus,12,214);checksums(receiptStatus);assert(!check(receiptStatus));
  auto outOfScreen=p;put16(outOfScreen,12,229);checksums(outOfScreen);assert(!strcmp(check(outOfScreen),"frame_bounds"));
  auto decoratedStatus=p;put16(decoratedStatus,40+11*6*12+2,0xF800);checksums(decoratedStatus);assert(!strcmp(check(decoratedStatus),"frame_status_background"));
  auto rawStatus=p;rawStatus[5]=0;rawStatus.resize(40+11*6*12+115200,0);put32(rawStatus,28,115200);checksums(rawStatus);assert(!check(rawStatus));
  put16(rawStatus,40+11*6*12+2*(224*240+198),0xF800);checksums(rawStatus);assert(!strcmp(check(rawStatus),"frame_status_background"));
  for(unsigned n=0;n<p.size();++n) {auto truncated=p;truncated.resize(n);assert(check(truncated,p.size()));}
  auto bad=p;bad.back()^=1;assert(!strcmp(check(bad),"frame_body_crc"));
  bad=p;put16(bad,40+11*6*12,0);checksums(bad);assert(!strcmp(check(bad),"frame_runs"));
  bad=p;put16(bad,40+11*6*12,65535);checksums(bad);assert(!strcmp(check(bad),"frame_runs"));
  bad=p;put16(bad,40+11*6*12,57599);checksums(bad);assert(!strcmp(check(bad),"frame_pixels"));
  bad=p;put16(bad,10,220);checksums(bad);assert(!strcmp(check(bad),"frame_bounds"));
  bad=p;p[14]=13;checksums(p);assert(!strcmp(check(p),"frame_bounds"));
  assert(themeBlend(0xF800,0,255)==0xF800);assert(themeBlend(0xF800,0,0)==0);
  assert(validApiSourceChange("https://server","https://server","old-key",""));
  assert(!validApiSourceChange("https://server","http://192.168.1.2","old-key","old-key"));
  assert(!validApiSourceChange("https://server","http://192.168.1.2","old-key",""));
  assert(validApiSourceChange("https://server","http://192.168.1.2","old-key","new-key"));
  assert(!validApiOrigin("http://010.0.0.1"));
  assert(!validApiOrigin("http://192.168.031.210:8012"));
  for (const char* origin:{"https://napotim.duckdns.org","http://192.168.31.210:8012","http://172.18.1.2:8000","http://10.0.0.1:80"}) assert(validApiOrigin(origin));
  for (const char* origin:{"http://10.evil.example","http://192.168.evil.example","http://8.8.8.8","http://192.168.256.1","http://192.168.1.1:65536","https://host:0","https://user@host","https://host/path","https://host:abc","http://172.32.1.1"}) assert(!validApiOrigin(origin));
  std::cout<<"theme packet corruption, truncation, bounds and blend checks passed\n";
}
