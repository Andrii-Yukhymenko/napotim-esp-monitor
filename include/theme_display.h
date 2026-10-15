#pragma once
#include <LittleFS.h>
#include <Adafruit_ST7789.h>
#include <memory>
#include <utility>
#include "theme_packet.h"

class ThemeDisplay {
 public:
  ThemeHeader header;
  String id, theme;
  std::unique_ptr<uint8_t[]> glyphs;
  bool active=false;
  String paintedStamp;
  bool paintedStale=false;
  void clear() {
    active=false;id="";theme="";paintedStamp="";glyphs.reset();
    LittleFS.remove("/theme.pending");LittleFS.remove("/theme.frame");
  }
  void activate(const ThemeHeader& h, const String& frameId, const String& themeId, std::unique_ptr<uint8_t[]> preparedGlyphs) {
    glyphs=std::move(preparedGlyphs);
    header=h;id=frameId;theme=themeId;active=true;paintedStamp="";
  }
  template<class Draw>
  bool rows(Draw draw) {
    File file=LittleFS.open("/theme.frame","r");
    if (!file || !file.seek(40+header.atlasBytes)) return false;
    uint16_t pixels[240];uint8_t run[4];unsigned remaining=0;uint16_t color=0;
    for (int y=0;y<240;++y) {
      if (!header.encoding) {
        if (file.read(reinterpret_cast<uint8_t*>(pixels),480)!=480) return false;
      } else {
        for (int x=0;x<240;++x) {
          if (!remaining) {
            if (file.read(run,4)!=4) return false;
            remaining=themeU16(run);color=themeU16(run+2);
            if (!remaining) return false;
          }
          pixels[x]=color;--remaining;
        }
      }
      draw(y,pixels);yield();
    }
    return true;
  }
  bool render(Adafruit_ST7789& tft) {
    if (!active) return false;
    paintedStamp="";
    return rows([&](int y,uint16_t* pixels) { tft.drawRGBBitmap(0,y,pixels,240,1); });
  }
  uint16_t statusPixel(const String& stamp, bool stale, unsigned col, unsigned row) const {
    unsigned cell=col/header.glyphWidth;
    if (stamp.length()!=5 || cell>=5) return header.bg;
    char ch=stamp[cell];unsigned index=ch==':'?10:ch-'0';
    if (index>10) return header.bg;
    unsigned offset=index*header.glyphWidth*header.glyphHeight+row*header.glyphWidth+col%header.glyphWidth;
    return themeBlend(stale?header.stale:header.ok,header.bg,glyphs[offset]);
  }
  void status(Adafruit_ST7789& tft,const String& stamp,bool stale,bool force=false) {
    if (!active || (!force && stamp==paintedStamp && stale==paintedStale)) return;
    uint16_t cell[12*20];
    for (unsigned i=0;i<5;++i) {
      if (!force && stale==paintedStale && paintedStamp.length()==5 && stamp.length()==5 && stamp[i]==paintedStamp[i]) continue;
      for (unsigned r=0;r<header.glyphHeight;++r)
        for (unsigned c=0;c<header.glyphWidth;++c)
          cell[r*header.glyphWidth+c]=statusPixel(stamp,stale,i*header.glyphWidth+c,r);
      tft.drawRGBBitmap(header.x+i*header.glyphWidth,header.y,cell,header.glyphWidth,header.glyphHeight);
    }
    paintedStamp=stamp;paintedStale=stale;
  }
};
