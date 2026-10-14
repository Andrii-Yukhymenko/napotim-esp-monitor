#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// NPTF/1 is shared with Napotim docs/display-themes.md. No Arduino dependencies.
constexpr unsigned THEME_HEADER_BYTES = 40, THEME_MAX_BYTES = 120000;
constexpr unsigned THEME_MAX_GLYPH_BYTES = 11 * 12 * 20;
inline uint16_t themeU16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
inline uint32_t themeU32(const uint8_t* p) { return themeU16(p) | (uint32_t(themeU16(p+2)) << 16); }
inline uint32_t themeCrc(uint32_t crc, const uint8_t* p, size_t n) {
  while (n--) { crc ^= *p++; for (int i=0;i<8;++i) crc=(crc>>1)^((crc&1)?0xEDB88320U:0); }
  return crc;
}
struct ThemeHeader {
  uint8_t encoding=0, glyphWidth=0, glyphHeight=0;
  uint16_t x=0,y=0,bg=0,ok=0,stale=0;
  uint32_t atlasBytes=0,pixelBytes=0,crc=0;
};
inline const char* parseThemeHeader(const uint8_t* p, unsigned total, ThemeHeader& h) {
  if (total<THEME_HEADER_BYTES || total>THEME_MAX_BYTES || memcmp(p,"NPTF",4) || p[4]!=1 || p[5]>1)
    return "frame_header";
  if ((themeCrc(0xFFFFFFFFU,p,36)^0xFFFFFFFFU)!=themeU32(p+36)) return "frame_header_crc";
  if (themeU16(p+6)!=240 || themeU16(p+8)!=240 || themeU16(p+22)!=0) return "frame_dimensions";
  h.encoding=p[5];h.x=themeU16(p+10);h.y=themeU16(p+12);h.glyphWidth=p[14];h.glyphHeight=p[15];
  h.bg=themeU16(p+16);h.ok=themeU16(p+18);h.stale=themeU16(p+20);
  h.atlasBytes=themeU32(p+24);h.pixelBytes=themeU32(p+28);h.crc=themeU32(p+32);
  if (h.ok==h.stale || h.bg==h.ok || h.bg==h.stale) return "frame_status_colors";
  if (!h.glyphWidth || h.glyphWidth>12 || !h.glyphHeight || h.glyphHeight>20 ||
      h.x+5*h.glyphWidth>240 || h.y+h.glyphHeight>240 ||
      h.atlasBytes!=11U*h.glyphWidth*h.glyphHeight || h.atlasBytes>THEME_MAX_GLYPH_BYTES ||
      h.pixelBytes>115200 || total!=40U+h.atlasBytes+h.pixelBytes ||
      (!h.encoding && h.pixelBytes!=115200) || (h.encoding && (!h.pixelBytes || h.pixelBytes%4)))
    return "frame_bounds";
  return nullptr;
}
template<class Reader, class Idle>
const char* validateThemePacket(Reader& reader, unsigned total, ThemeHeader& h, Idle idle) {
  uint8_t header[40];
  if (reader.read(header,40)!=40) return "frame_truncated";
  const char* error=parseThemeHeader(header,total,h);if (error) return error;
  uint32_t crc=0xFFFFFFFFU;
  uint8_t chunk[256];unsigned left=h.atlasBytes;uint16_t nonemptyGlyphs=0;
  while (left) {
    unsigned n=left>sizeof(chunk)?sizeof(chunk):left;
    if (reader.read(chunk,n)!=int(n)) return "frame_truncated";
    for(unsigned i=0;i<n;++i) if(chunk[i]) nonemptyGlyphs|=uint16_t(1U<<((h.atlasBytes-left+i)/(h.glyphWidth*h.glyphHeight)));
    crc=themeCrc(crc,chunk,n);left-=n;idle();
  }
  left=h.pixelBytes;unsigned pixels=0;bool statusBackgroundValid=true;
  auto statusOverlap=[&](unsigned start,unsigned count) {
    unsigned end=start+count;
    for (unsigned row=h.y;row<h.y+h.glyphHeight;++row) {
      unsigned at=row*240+h.x;
      if (start<at+5*h.glyphWidth && end>at) return true;
    }
    return false;
  };
  while (left) {
    unsigned n=left>sizeof(chunk)?sizeof(chunk):left;
    if (reader.read(chunk,n)!=int(n)) return "frame_truncated";
    crc=themeCrc(crc,chunk,n);
    if (h.encoding) for (unsigned i=0;i<n;i+=4) {
      unsigned run=themeU16(chunk+i);
      if (!run || run>57600U-pixels) return "frame_runs";
      if (themeU16(chunk+i+2)!=h.bg && statusOverlap(pixels,run)) statusBackgroundValid=false;
      pixels+=run;
    }
    else for(unsigned i=0;i<n;i+=2) {
      if (themeU16(chunk+i)!=h.bg && statusOverlap(pixels,1)) statusBackgroundValid=false;
      ++pixels;
    }
    left-=n;idle();
  }
  if (pixels!=57600) return "frame_pixels";
  if ((crc^0xFFFFFFFFU)!=h.crc) return "frame_body_crc";
  if (nonemptyGlyphs!=0x07FF) return "frame_glyphs";
  if (!statusBackgroundValid) return "frame_status_background";
  return nullptr;
}
inline uint16_t themeBlend(uint16_t fg,uint16_t bg,uint8_t alpha) {
  uint16_t r=(((fg>>11)*alpha+(bg>>11)*(255-alpha)+127)/255);
  uint16_t g=((((fg>>5)&63)*alpha+((bg>>5)&63)*(255-alpha)+127)/255);
  uint16_t b=(((fg&31)*alpha+(bg&31)*(255-alpha)+127)/255);
  return (r<<11)|(g<<5)|b;
}
