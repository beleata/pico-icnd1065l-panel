#include "ota_journal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>

static jmp_buf reboot;
struct MockFlash {
    uint8_t bytes[0x200000];
    int until_cut=-1;unsigned partial=0,cuts=0;
    const uint8_t *ptr(uint32_t at)const{return bytes+at;}
    bool write(uint32_t at,const uint8_t *p,uint32_t n,bool erase) {
        assert(at+n<=sizeof(bytes));
        bool cut=until_cut==0;
        if(until_cut>0)--until_cut;
        unsigned count=cut?(partial<n?partial:n):n;
        for(unsigned i=0;i<count;++i) {if(erase)bytes[at+i]=255;else bytes[at+i]&=p[i];}
        if(cut){++cuts;until_cut=-1;longjmp(reboot,1);}return true;
    }
    bool erase(uint32_t at,uint32_t n){return write(at,nullptr,n,true);}
    bool program(uint32_t at,const uint8_t*p,uint32_t n){return write(at,p,n,false);}
};
static MockFlash flash;
static uint8_t buffer[ota::SECTOR];
static uint8_t pattern(unsigned i,bool newer){return uint8_t((i*31+(i>>12)*7+(newer?103:17))%251);}
int main() {
    memset(flash.bytes,255,sizeof(flash.bytes));
    for(unsigned i=0;i<ota::SLOT;++i){flash.bytes[ota::APP+i]=pattern(i,false);flash.bytes[ota::DOWNLOAD+i]=pattern(i,true);}
    ota::BootState s;s.mode=ota::UPDATE;assert(ota::save(flash,s));
    volatile unsigned boots=0;
    for(;;) {
        setjmp(reboot);
        s=ota::state(flash);
        if(s.mode==ota::TRIAL)break;
        assert(++boots<10000);
        // Interrupt erase, page writes and journal records at different byte boundaries.
        flash.until_cut=3+(boots%13);flash.partial=(boots*127)%4097;
        while(s.mode==ota::UPDATE)assert(ota::swap_step(flash,s,buffer));
    }
    flash.until_cut=-1;
    for(unsigned i=0;i<ota::SLOT;++i){assert(flash.bytes[ota::APP+i]==pattern(i,true));assert(flash.bytes[ota::DOWNLOAD+i]==pattern(i,false));}
    s.mode=ota::ROLLBACK;assert(ota::save(flash,s));
    while(s.mode==ota::ROLLBACK)assert(ota::swap_step(flash,s,buffer));
    for(unsigned i=0;i<ota::SLOT;++i){assert(flash.bytes[ota::APP+i]==pattern(i,false));assert(flash.bytes[ota::DOWNLOAD+i]==pattern(i,true));}
    // Settings journal: an interrupted write must yield old or new, never mixed.
    ota::Journal<MockFlash> settings(flash,ota::SETTINGS,1234);
    uint32_t value=1;assert(settings.save(&value,4));
    for(unsigned cut=0;cut<256;++cut) {
        if(!setjmp(reboot)){value=cut+2;flash.until_cut=0;flash.partial=cut;settings.save(&value,4);}
        flash.until_cut=-1;ota::Record r;assert(settings.latest(r)>=0);
        uint32_t read;memcpy(&read,r.payload,4);assert(read==cut+1 || read==cut+2);
        value=cut+2;assert(settings.save(&value,4));
    }
    printf("PASS resumable update (%u simulated power cuts), rollback, torn settings writes\n",flash.cuts);
}
