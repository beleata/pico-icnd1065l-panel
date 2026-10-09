#pragma once
#include "solar_decoder.h"
#include "video_frame.h"
#include "solar_settings.h"
#include <stdio.h>

namespace solar {
struct State {
    Reading site{}, batteries[2]{};
    uint64_t site_at=0, battery_at[2]={};
    bool site_valid=false, battery_valid[2]={};
    void ingest(const Reading &r,uint64_t now) {
        if(r.unit==100) {
            site=r; site_at=now; site_valid=true;
        }
        if(r.unit==2 || r.unit==3) {
            unsigned i=r.unit-2; batteries[i]=r; battery_at[i]=now; battery_valid[i]=true;
        }
    }
};
inline bool stale(bool valid,uint64_t at,uint64_t now) { return valid && now-at>=600000000ull; }
struct Color { uint8_t r,g,b; };
constexpr Color WHITE{180,180,180}, YELLOW{210,180,0}, BLUE{0,140,220},
    GREEN{0,190,60}, PINK{200,40,100}, RED{180,0,0}, UNKNOWN{70,70,70};
inline bool operator==(Color a,Color b) { return a.r==b.r && a.g==b.g && a.b==b.b; }
inline bool fresh(bool valid,uint64_t at,uint64_t now,bool connected) {
    return connected && valid && !stale(valid,at,now);
}
inline unsigned average_soc(const State &s) {
    // Equal arithmetic mean, not weighted by the different battery capacities.
    return (unsigned(s.batteries[0].soc10)+s.batteries[1].soc10+10)/20;
}
inline Color battery_color(const State &s,bool soc_ready,bool site_ready) {
    if(!soc_ready) return UNKNOWN;
    if(average_soc(s)==100) return GREEN;
    if(unsigned(s.batteries[0].soc10)+s.batteries[1].soc10<200) return RED;
    if(!site_ready) return UNKNOWN;
    return s.site.battery<0?RED:GREEN;
}
inline Color charge_icon_color(const State &s,bool ready) {
    if(!ready) return UNKNOWN;
    unsigned sum=unsigned(s.batteries[0].soc10)+s.batteries[1].soc10;
    return sum<200?RED:sum<400?YELLOW:GREEN;
}
inline Color pv_color(const Reading &r) {
    if(r.pv>=r.load) return GREEN;
    if(r.grid>0) return RED;
    if(r.battery<0) return YELLOW;
    return UNKNOWN; // A deficit without measured battery discharge/import is unresolved.
}
inline Color grid_color(int32_t watts) { return watts<0?YELLOW:watts>0?RED:WHITE; }

// A small Bulgarian bitmap alphabet, five rows of three pixels per glyph.
inline const char *glyph(uint32_t c) {
    switch(c) {
    case '0': return "111101101101111"; case '1': return "010110010010111";
    case '2': return "111001111100111"; case '3': return "111001111001111";
    case '4': return "101101111001001"; case '5': return "111100111001111";
    case '6': return "111100111101111"; case '7': return "111001010100100";
    case '8': return "111101111101111"; case '9': return "111101111001111";
    case '.': return "000000000000010"; case '-': return "000000111000000";
    case '%': return "101001010100101";
    case 'W': return "101101111111101"; case 'F': return "111100110100100";
    case 'I': return "111010010010111";
    case 'K': case 0x41a: return "101110100110101";
    case 0x410: return "010101111101101"; case 0x411: return "111100111101111";
    case 0x412: return "110101110101110"; case 0x413: return "111100100100100";
    case 0x414: return "011101101111101"; case 0x415: return "111100110100111";
    case 0x416: return "101101010101101"; case 0x417: return "110001010001110";
    case 0x418: return "101101111111101"; case 0x41b: return "011101101101101";
    case 0x41c: return "101111111101101"; case 0x41d: return "101101111101101";
    case 0x41e: return "111101101101111"; case 0x41f: return "111101101101101";
    case 0x420: return "111101111100100"; case 0x421: return "111100100100111";
    case 0x422: return "111010010010010"; case 0x423: return "101101111001110";
    case 0x424: return "010111111111010"; case 0x425: return "101101010101101";
    case 0x426: return "101101101111001"; case 0x427: return "101101111001001";
    case 0x428: return "101101111111111"; case 0x429: return "101101111111001";
    case 0x42a: return "110010011011011"; case 0x42c: return "100100110101110";
    case 0x42e: return "101111111111101"; case 0x42f: return "111101111011101";
    default: return "000000000000000";
    }
}
inline void pixel(uint8_t *frame,int x,int y,Color c) {
    if(x<0 || x>=64 || y<0 || y>=32) return;
    unsigned col=(unsigned(x)/32)*64 + (y%16<8?32:0) + unsigned(x)%32;
    unsigned word=((unsigned(y)%8)*16+col%16)*8+col/16;
    unsigned plane=(unsigned(y)/16)*3;
    frame[plane*1024+word]=c.r; frame[(plane+1)*1024+word]=c.g; frame[(plane+2)*1024+word]=c.b;
}
inline void text(uint8_t *frame,const char *s,int x,int y,Color c) {
    while(*s) {
        uint32_t ch=uint8_t(*s++);
        if((ch&0xe0)==0xc0 && *s) { ch=((ch&31)<<6)|(uint8_t(*s++)&63); }
        const char *bits=glyph(ch);
        for(int j=0;j<5;++j) for(int i=0;i<3;++i) if(bits[j*3+i]=='1') pixel(frame,x+i,y+j,c);
        x+=4;
    }
}
inline uint32_t whole_kw(int32_t watts) {
    uint32_t magnitude=uint32_t(watts<0?-int64_t(watts):watts);
    return (magnitude+500)/1000;
}
inline const char *digit7(char c) {
    switch(c) {
    case '0':return "01110100011000110001100011000101110";
    case '1':return "00100011000010000100001000010001110";
    case '2':return "01110100010000100010001000100011111";
    case '3':return "11110000010000101110000010000111110";
    case '4':return "00010001100101010010111110001000010";
    case '5':return "11111100001000011110000010000111110";
    case '6':return "01110100001000011110100011000101110";
    case '7':return "11111000010001000100010000100001000";
    case '8':return "01110100011000101110100011000101110";
    case '9':return "01110100011000101111000010000101110";
    default:return "00000000000000011111000000000000000";
    }
}
inline const char *digit4(char c) {
    switch(c) {
    case '0':return "0110" "1001" "1001" "1001" "1001" "1001" "0110";
    case '1':return "0010" "0110" "0010" "0010" "0010" "0010" "0111";
    case '2':return "0110" "1001" "0001" "0010" "0100" "1000" "1111";
    case '3':return "1110" "0001" "0001" "0110" "0001" "0001" "1110";
    case '4':return "0010" "0110" "1010" "1010" "1111" "0010" "0010";
    case '5':return "1111" "1000" "1000" "1110" "0001" "0001" "1110";
    case '6':return "0110" "1000" "1000" "1110" "1001" "1001" "0110";
    case '7':return "1111" "0001" "0010" "0010" "0100" "0100" "0100";
    case '8':return "0110" "1001" "1001" "0110" "1001" "1001" "0110";
    case '9':return "0110" "1001" "1001" "0111" "0001" "0001" "0110";
    default:return "0000" "0000" "0000" "1111" "0000" "0000" "0000";
    }
}
constexpr char PERCENT_ICON[]=
    "1100001" "1010010" "1100010" "0000100" "0001000" "0001000"
    "0010000" "0100000" "0100011" "1000101" "1000011";
constexpr char UNIT_K[]="100" "100" "101" "110" "110" "101" "101";
constexpr char UNIT_W[]="10001" "10001" "10001" "10001" "10101" "10101" "01010";
static_assert(sizeof(PERCENT_ICON)==7*11+1 && sizeof(UNIT_K)==3*7+1 && sizeof(UNIT_W)==5*7+1);
inline void bitmap(uint8_t *f,const char *bits,int width,int height,int x,int y,Color c,int sx=1,int sy=1) {
    for(int j=0;j<height;++j) for(int i=0;i<width;++i) if(bits[j*width+i]=='1')
        for(int dy=0;dy<sy;++dy) for(int dx=0;dx<sx;++dx) pixel(f,x+i*sx+dx,y+j*sy+dy,c);
}
inline void battery_icon(uint8_t *f,int x,Color c,unsigned percent=0) {
    for(int i=1;i<4;++i) pixel(f,x+i,1,c);
    for(int i=0;i<5;++i) { pixel(f,x+i,3,c); pixel(f,x+i,12,c); }
    for(int y=3;y<=12;++y) { pixel(f,x,y,c); pixel(f,x+4,y,c); }
    unsigned rows=(percent*8+50)/100;
    for(unsigned row=0;row<rows;++row) for(int i=1;i<4;++i) pixel(f,x+i,11-int(row),c);
}
// Fixed 32x16 cells; four-character signed values use a narrower fallback.
inline void number(uint8_t *f,uint32_t amount,bool valid,bool negative,const char *unit,
                   int x,int y,Color c,bool wide=false) {
    char value[16];
    if(valid) snprintf(value,sizeof(value),"%s%lu",negative?"-":"",(unsigned long)amount);
    else snprintf(value,sizeof(value),"--");
    bool compact=strlen(value)>3;
    bool narrow_kw=!strcmp(unit,"KW") && strlen(value)==3 && !wide;
    int width=compact?3:narrow_kw?4:5, height=compact?5:7, sx=wide?2:1;
    int top=y+(compact?3:1);
    for(const char *s=value;*s;++s) {
        bitmap(f,compact?glyph(*s):narrow_kw?digit4(*s):digit7(*s),width,height,x,top,c,sx,2);
        x+=(width+1)*sx;
    }
    if(!strcmp(unit,"%")) bitmap(f,PERCENT_ICON,7,11,x,y+2,WHITE);
    else if(!strcmp(unit,"KW")) {
        bitmap(f,UNIT_K,3,7,x,y+7,WHITE);
        bitmap(f,UNIT_W,5,7,x+4,y+7,WHITE);
    } else text(f,unit,x,y+10,c);
}
inline Color rgb(uint32_t c) { return {uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)}; }
inline unsigned pixel_offset(unsigned x,unsigned y) {
    unsigned col=(x/32)*64+(y%16<8?32:0)+x%32;
    return (y/16)*3072+((y%8)*16+col%16)*8+col/16;
}
inline void render(const State &s,bool connected,uint64_t now,uint8_t *f,const Settings &settings=Settings{}) {
    memset(f,0,video::FRAME_BYTES);
    bool site_ready=fresh(s.site_valid,s.site_at,now,connected);
    bool soc_ready=fresh(s.battery_valid[0],s.battery_at[0],now,connected) &&
                   fresh(s.battery_valid[1],s.battery_at[1],now,connected);
    Color battery=battery_color(s,soc_ready,site_ready);
    Color sun=site_ready?pv_color(s.site):UNKNOWN;
    Color grid=site_ready?(s.site.grid<0?rgb(settings.export_color):s.site.grid>0?rgb(settings.import_color):WHITE):UNKNOWN;
    Color load=site_ready?rgb(settings.load):UNKNOWN;
    battery_icon(f,0,charge_icon_color(s,soc_ready),soc_ready?average_soc(s):0);
    number(f,average_soc(s),soc_ready,false,"%",7,0,battery);
    bitmap(f,"00100101010111011111011101010100100",5,7,32,4,YELLOW);
    number(f,whole_kw(s.site.pv),site_ready,false,"KW",39,0,sun);
    int house_x=settings.swap_lower?32:0,grid_x=settings.swap_lower?0:32;
    bitmap(f,"0001000001110001111101111111100000110111011010101",7,7,house_x,20,GREEN);
    number(f,whole_kw(s.site.load),site_ready,false,"KW",house_x+8,16,load);
    bitmap(f,"0011000110011001111100110011000100001000",5,8,grid_x,20,RED);
    number(f,whole_kw(s.site.grid),site_ready,false,"KW",grid_x+7,16,grid);
    if(settings.rotate) for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x) {
        unsigned a=pixel_offset(x,y),b=pixel_offset(63-x,31-y);
        for(unsigned c=0;c<3;++c) {uint8_t t=f[a+c*1024];f[a+c*1024]=f[b+c*1024];f[b+c*1024]=t;}
    }
    if(settings.brightness!=100)for(unsigned i=0;i<video::FRAME_BYTES;++i)
        f[i]=uint8_t((unsigned(f[i])*settings.brightness+50)/100);
}
}
