#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace ota {
constexpr uint32_t APP=0xa000, SLOT=984*1024, DOWNLOAD=0x100000;
constexpr uint32_t JOURNAL=0x1f6000, SCRATCH=0x1f8000, SETTINGS=0x1f9000;
constexpr uint32_t SECTOR=4096, PAGE=256;
inline uint32_t crc32(const void *data,size_t n) {
    auto p=static_cast<const uint8_t*>(data); uint32_t c=~0u;
    while(n--) { c^=*p++; for(int b=0;b<8;++b)c=(c>>1)^((0u-(c&1))&0xedb88320u); }
    return ~c;
}
struct Record {
    uint32_t magic=0, generation=0;
    uint8_t payload[244]={};
    uint32_t crc=0;
};
static_assert(sizeof(Record)==PAGE);
// Append-only, two-sector journal. The sector containing the latest valid
// record is never erased until a newer verified record exists elsewhere.
template<class Flash> class Journal {
    Flash &flash; uint32_t base,magic;
public:
    Journal(Flash &f,uint32_t b,uint32_t m):flash(f),base(b),magic(m){}
    int latest(Record &out) const {
        int found=-1; out=Record{};
        for(int i=0;i<32;++i) {
            const Record *r=reinterpret_cast<const Record*>(flash.ptr(base+i*PAGE));
            if(r->magic==magic && r->generation && r->crc==crc32(r,252) &&
               (found<0 || r->generation>out.generation)) { out=*r; found=i; }
        }
        return found;
    }
    bool save(const void *payload,size_t n) {
        if(n>244)return false;
        Record old; int i=latest(old), next=i<0?0:i+1;
        if(old.generation==UINT32_MAX)return false;
        auto erased=[&](int page) {
            const uint8_t *p=flash.ptr(base+page*PAGE);
            for(unsigned b=0;b<PAGE;++b)if(p[b]!=255)return false;
            return true;
        };
        if(i<0) { if(!flash.erase(base,SECTOR))return false; }
        else {
            int limit=(i/16+1)*16;
            while(next<limit && !erased(next))++next;
            if(next==limit) {
                next=i<16?16:0;
                if(!flash.erase(base+next*PAGE,SECTOR))return false;
            }
        }
        Record r; r.magic=magic;r.generation=old.generation+1;
        memcpy(r.payload,payload,n);r.crc=crc32(&r,252);
        if(!flash.program(base+next*PAGE,reinterpret_cast<const uint8_t*>(&r),PAGE))return false;
        return !memcmp(flash.ptr(base+next*PAGE),&r,PAGE);
    }
};
enum Mode:uint32_t { IDLE=0, UPDATE=1, TRIAL=2, ROLLBACK=3 };
struct BootState { uint32_t mode=IDLE,index=0,phase=0,backup=0; };
constexpr uint32_t BOOT_MAGIC=0x3141544f;
template<class Flash> BootState state(Flash &f) {
    Journal<Flash> j(f,JOURNAL,BOOT_MAGIC);Record r;BootState s;
    if(j.latest(r)>=0)memcpy(&s,r.payload,sizeof(s));
    return s;
}
template<class Flash> bool save(Flash &f,const BootState&s) {
    Journal<Flash> j(f,JOURNAL,BOOT_MAGIC);return j.save(&s,sizeof(s));
}
// One durable step. Scratch retains the old application sector until both
// destinations are verified. Retrying any interrupted step is idempotent.
template<class Flash> bool swap_step(Flash &f,BootState &s,uint8_t *buffer) {
    if(s.mode!=UPDATE && s.mode!=ROLLBACK)return false;
    if(s.index>=SLOT/SECTOR || s.phase>2)return false;
    uint32_t a=APP+s.index*SECTOR,b=DOWNLOAD+s.index*SECTOR;
    uint32_t src=s.phase==0?a:s.phase==1?b:SCRATCH;
    uint32_t dst=s.phase==0?SCRATCH:s.phase==1?a:b;
    memcpy(buffer,f.ptr(src),SECTOR);
    if(!f.erase(dst,SECTOR) || !f.program(dst,buffer,SECTOR) || memcmp(f.ptr(dst),buffer,SECTOR))return false;
    if(++s.phase==3) {s.phase=0;++s.index;}
    if(s.index==SLOT/SECTOR) {
        s.mode=s.mode==UPDATE?TRIAL:IDLE;s.index=0;s.backup=1;
    }
    return save(f,s);
}
}
