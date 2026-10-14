"""Generate independent golden pixels from the pre-theme 8c7b2cc C++ renderer.

Requires a PlatformIO build (pinned U8g2 library), g++, and Pillow. No device I/O.
Usage: backend/venv/bin/python tests/render_original_display.py --output DIR
"""
import argparse,json,re,subprocess,tempfile
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
REVISION='8c7b2cc'
LIB=ROOT/'.pio/libdeps/ultra/U8g2_for_Adafruit_GFX/src'

ADAPTER=r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
class Print {
 public:
  virtual size_t write(uint8_t)=0;
  void print(const std::string& value){for(unsigned char ch:value)write(ch);}
};
class Adafruit_GFX:public Print {
 public:
  int16_t width,height;
  Adafruit_GFX(int16_t w,int16_t h):width(w),height(h){}
  virtual void drawPixel(int16_t,int16_t,uint16_t)=0;
  size_t write(uint8_t)override{return 1;}
  void drawFastHLine(int16_t x,int16_t y,int16_t n,uint16_t c){for(int i=0;i<n;++i)drawPixel(x+i,y,c);}
  void drawFastVLine(int16_t x,int16_t y,int16_t n,uint16_t c){for(int i=0;i<n;++i)drawPixel(x,y+i,c);}
  void fillScreen(uint16_t c){for(int y=0;y<height;++y)drawFastHLine(0,y,width,c);}
};
'''
PREFIX=r'''
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cassert>
#include <U8g2_for_Adafruit_GFX.h>
#include "update_status.h"
using std::min;using std::max;
#define PROGMEM
#define pgm_read_word(p) (*(p))
#define pgm_read_byte(p) (*(p))
class String:public std::string {
 public:
  using std::string::string;
  String(const std::string& v):std::string(v){}
  String(int v):std::string(std::to_string(v)){}
  void remove(size_t at){erase(at);}
  String substring(size_t at,size_t end=std::string::npos)const{return substr(at,end==std::string::npos?end:end-at);}
  void replace(const std::string& from,const std::string& to){size_t at=0;while((at=find(from,at))!=npos){std::string::replace(at,from.size(),to);at+=to.size();}}
};
String operator+(const std::string& value,int number){return String(value+std::to_string(number));}
constexpr uint16_t BG=0x0841,MUTED=0x9CF3,WHITE=0xEF7D,PURPLE=0x795B,RED=0xF924,RED_TITLE_EDGE=0x68A2;
class Screen:public Adafruit_GFX {
 public:
  std::array<uint16_t,57600> pixels;
  Screen():Adafruit_GFX(240,240){pixels.fill(BG);}
  void drawPixel(int16_t x,int16_t y,uint16_t c)override{if(x>=0&&x<240&&y>=0&&y<240)pixels[y*240+x]=c;}
  void startWrite(){}void endWrite(){}
  void writePixel(int x,int y,uint16_t c){drawPixel(x,y,c);}
  void writeFastHLine(int x,int y,int n,uint16_t c){drawFastHLine(x,y,n,c);}
  void fillRect(int x,int y,int w,int h,uint16_t c){for(int r=0;r<h;++r)drawFastHLine(x,y+r,w,c);}
  void drawRect(int x,int y,int w,int h,uint16_t c){drawFastHLine(x,y,w,c);drawFastHLine(x,y+h-1,w,c);drawFastVLine(x,y,h,c);drawFastVLine(x+w-1,y,h,c);}
  void fillCircle(int x,int y,int radius,uint16_t c){assert(radius==2);for(int dy=-2;dy<=2;++dy){int extent=std::abs(dy)==2?1:2;drawFastHLine(x-extent,y+dy,extent*2+1,c);}}
  uint16_t color565(uint8_t r,uint8_t g,uint8_t b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
  void drawRGBBitmap(int x,int y,uint16_t* p,int w,int h){for(int r=0;r<h;++r)for(int col=0;col<w;++col)drawPixel(x+col,y+r,p[r*w+col]);}
}tft;
U8G2_FOR_ADAFRUIT_GFX font;
struct Task{String title,category,dueDate,dueTime;uint16_t color;bool high,overdue,today;};
Task tasks[6];uint8_t taskCount=0;
int hiddenCount=0,todayTotal=0,overdueTotal=0;
String date,failure;
struct NightMode{enum State{Awake};State state=Awake;}nightMode;
constexpr int WL_CONNECTED=1;
struct FakeWifi{int status(){return WL_CONNECTED;}}WiFi;
time_t lastSuccess=1791898920;
bool stale=false;
bool updateStale(){return stale;}
int localOffset(time_t){return 0;}
void yield(){}
'''

def quote(value):return json.dumps(value,ensure_ascii=False)

def cases():
    names=['Надіслати кошторис','Оплатити рахунок','Зібрати прототип','Купити продукти','Полити рослини','Прочитати 20 сторінок']
    colors=['#C29BE6','#EFCC70','#C29BE6','#EFCC70','#8CE8C0','#80B5F0']
    cats=['Робота','Побут','Робота','Побут','Дім','Особисте']
    times=['18:00','','14:00','18:30','20:00','']
    values=[]
    for name in ('normal','empty','long'):
        lines=[f'date="2026-10-13";todayTotal={0 if name=="empty" else 6};overdueTotal={0 if name=="empty" else 2};taskCount={0 if name=="empty" else 6};hiddenCount={0 if name=="empty" else 2};']
        for i,title in enumerate(names):
            if name=='long':title='Їжак ґрунтовно перевіряє надзвичайно довге українське завдання та його відображення'
            category='Дуже довга категорія' if name=='long' else cats[i]
            lines.append(f'tasks[{i}]={{ {quote(title)},{quote(category)},"2026-10-{12 if i<2 else 13}",{quote(times[i])},categoryColor("{colors[i]}"),{str(i==0).lower()},{str(i<2).lower()},{str(i>=2).lower()} }};')
        values.append('\n'.join(lines))
    return values

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    source=subprocess.check_output(['git','show',REVISION+':src/main.cpp'],cwd=ROOT,text=True)
    titlefont=subprocess.check_output(['git','show',REVISION+':include/title_font.h'],cwd=ROOT,text=True).replace('#pragma once','').replace('#include <Arduino.h>','')
    functions=source[source.index('String clip('):source.index('bool updateStale()')]+source[source.index('constexpr int UPDATE_GLYPH_WIDTH'):source.index('void drawSetup(')]
    fonts=(LIB/'u8g2_fonts.c').read_text(encoding='latin1')
    native='\n'.join(re.search(r'const uint8_t u8g2_font_'+name+r'_t_cyrillic\[\d+\][^=]+='r'\s*(?:"(?:[^"\\]|\\.)*"\s*)+;',fonts)[0] for name in ('6x12','6x13'))
    branches='\n'.join(f'if(which=={i}){{{value}}}' for i,value in enumerate(cases()))
    maincpp='int main(int argc,char** argv){assert(argc==3);int which=std::atoi(argv[1]);stale=std::atoi(argv[2]);font.begin(tft);font.setFontMode(1);font.setFontDirection(0);'+branches+'''
    drawHeader(false);for(int i=0;i<6;++i)drawRow(i,false);
    if(!taskCount){font.setFont(u8g2_font_6x13_t_cyrillic);textAt(15,90,"На сьогодні все виконано",WHITE);}
    drawFooter(true,false);for(auto pixel:tft.pixels){std::putchar(pixel&255);std::putchar(pixel>>8);} }
    '''
    with tempfile.TemporaryDirectory(prefix='original-display-') as name:
        temp=Path(name);(temp/'Adafruit_GFX.h').write_text(ADAPTER);(temp/'reference.cpp').write_text(PREFIX+titlefont+native+functions+maincpp)
        binary=temp/'reference'
        subprocess.run(['g++','-std=c++17','-O2','-I',str(temp),'-I',str(LIB),'-I',str(ROOT/'include'),str(temp/'reference.cpp'),str(LIB/'U8g2_for_Adafruit_GFX.cpp'),'-o',str(binary)],check=True)
        for i,case in enumerate(('normal','empty','long')):
            for stale_value in (0,1):
                raw=subprocess.check_output([str(binary),str(i),str(stale_value)])
                assert len(raw)==115200
                pixels=[]
                for at in range(0,len(raw),2):
                    color=raw[at]|raw[at+1]<<8;r,g,b=color>>11,(color>>5)&63,color&31
                    pixels.extend(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)))
                Image.frombytes('RGB',(240,240),bytes(pixels)).save(args.output/f'classic-{case}-{"stale" if stale_value else "fresh"}.png')
    print('Generated six golden frames from original firmware '+REVISION)

if __name__=='__main__':main()
