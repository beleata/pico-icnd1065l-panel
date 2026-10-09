#pragma once
#include <stdint.h>
namespace chain_arrow {
inline bool requested=false;
inline uint16_t samples[64][4][32];
inline void start() { requested=true; }
inline bool pixel(int x,int y,int left) {
    int dx=x-left,dy=y-15;
    if(dy<0)dy=-dy;
    return (dx>=0 && dx<17 && dy<=2) || (dx>=12 && dx<=27 && dy<=27-dx);
}
inline unsigned mask(unsigned word,int left) {
    unsigned row=word/512,channel=(word/32)%16,chip=word%32;
    unsigned serial=(chip%8)*16+channel;
    int x=(3-chip/8)*64+63-((serial/64)*32+serial%32);
    int y=31-(row+((serial/32)%2==0?8:0));
    return unsigned(pixel(x,y,left)) | (unsigned(pixel(x,y-16,left))<<1);
}
inline void init() {
    for(unsigned phase=0;phase<64;++phase)for(unsigned mask=0;mask<4;++mask)
        for(unsigned bit=0;bit<16;++bit) {
            unsigned clock=phase*16+bit,p=clock%128;
            uint16_t value=(((clock+16)/128)%8)<<6;
            if(p>=100 && p<104)value|=1u<<11;
            if(clock%512==511)value|=1u<<10;
            if((75u>>(15-bit))&1u)value|=(mask&1?7:0)|(mask&2?56:0);
            samples[phase][mask][bit*2]=value;
            samples[phase][mask][bit*2+1]=value|(1u<<9);
        }
}
}
