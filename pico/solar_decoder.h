#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Port of Solar/pico/solar_protocol.py. Only the observed SPC119 profiles.
namespace solar {
inline uint16_t u16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
inline uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) << 16 | u16(p+2); }
struct Reading {
    uint16_t unit = 0;
    uint32_t stamp = 0;
    int32_t pv = 0, load = 0, grid = 0, battery = 0;
    int32_t ac = 0;
    uint16_t hz10 = 0, soc10 = 0, unit_soc10 = 0;
    uint32_t capacity = 0;
    int32_t unit_battery = 0;
};
using Sink = void (*)(const Reading &, void *);
inline bool power(int32_t v) { return v > -200000 && v < 200000; }
inline unsigned decode(const uint8_t *a, size_t n, Sink sink, void *ctx) {
    if (n < 19 || a[2] || a[3] || size_t(u16(a+4))+6 != n ||
        a[6]!=100 || a[7]!=0x41 || a[8]!=0x47 || size_t(u16(a+9))+11!=n) return 0;
    const unsigned units[]={6,2,3,100}, sizes[]={351,647,717,57}, offsets[]={0,397,467,0};
    unsigned count=0;
    for (unsigned p=0;p<4;++p) {
        for (size_t i=13;i+6+sizes[p]<=n;++i) {
            if (u16(a+i)!=units[p] || u16(a+i+2)!=0x100 || u16(a+i+4)!=sizes[p]) continue;
            const uint8_t *b=a+i+6;
            Reading r; r.unit=units[p]; r.stamp=u32(b);
            if (r.stamp<1577836800u || r.stamp>4102444800u) break;
            if (r.unit==100) {
                r.pv=int32_t(u32(b+17)); r.load=int32_t(u32(b+21));
                r.grid=int32_t(u32(b+25)); r.battery=int32_t(u32(b+29));
                if (r.pv<0 || r.load<0 || !power(r.pv) || !power(r.load) || !power(r.grid) || !power(r.battery)) break;
            } else {
                r.pv=int32_t(u32(b+113)); r.ac=int32_t(u32(b+145)); r.hz10=u16(b+155);
                if (!power(r.pv) || !power(r.ac) || !(r.hz10==0 || (r.hz10>4500 && r.hz10<6500))) break;
                if (offsets[p]) {
                    unsigned o=offsets[p]; r.capacity=u32(b+o); r.soc10=u16(b+o+4);
                    r.unit_soc10=u16(b+319); r.battery=int32_t(u32(b+o+8)); r.unit_battery=int32_t(u32(b+313));
                    if (!r.capacity || r.capacity>200000 || r.soc10>1000 || r.unit_soc10>1000) break;
                }
            }
            ++count; if (sink) sink(r,ctx); break;
        }
    }
    return count;
}

struct Stream {
    static constexpr size_t CAP=8192, SEG=1536, SLOTS=4;
    uint8_t bytes[CAP]; size_t size=0;
    uint32_t next=0; bool initialized=false;
    struct Pending { uint32_t seq=0; size_t size=0; uint8_t bytes[SEG]; } pending[SLOTS];
    void reset() { size=0; initialized=false; for(auto &p:pending) p.size=0; }
    void parse(Sink sink, void *ctx) {
        size_t pos=0;
        while (size-pos>=9) {
            auto b=bytes+pos; size_t len=u16(b+4)+6;
            if (b[2] || b[3] || b[6]!=100 || (b[7]!=3 && b[7]!=0x41 && b[7]!=0x83 && b[7]!=0xc1) || len<9 || len>CAP) { ++pos; continue; }
            if (size-pos<len) break;
            decode(b,len,sink,ctx); pos+=len;
        }
        if(pos) { size-=pos; memmove(bytes,bytes+pos,size); }
    }
    bool append(const uint8_t *p,size_t n,Sink sink,void *ctx) {
        if(n>CAP-size) { reset(); return false; }
        memcpy(bytes+size,p,n); size+=n; next+=uint32_t(n); parse(sink,ctx); return true;
    }
    bool feed(uint32_t seq,const uint8_t *p,size_t n,Sink sink,void *ctx) {
        if(!n) return true;
        if(!initialized) { next=seq; initialized=true; }
        int32_t delta=int32_t(seq-next);
        if(delta>0) {
            if(n>SEG) { reset(); return false; }
            for(auto &q:pending) if(q.size && q.seq==seq && q.size==n) return true;
            for(auto &q:pending) if(!q.size) { q.seq=seq; q.size=n; memcpy(q.bytes,p,n); return true; }
            reset(); return false;
        }
        size_t trim=uint32_t(next-seq);
        if(trim<n && !append(p+trim,n-trim,sink,ctx)) return false;
        bool progress=true;
        while(progress) {
            progress=false;
            for(auto &q:pending) if(q.size && int32_t(q.seq-next)<=0) {
                size_t skip=uint32_t(next-q.seq), len=q.size; q.size=0;
                if(skip<len && !append(q.bytes+skip,len-skip,sink,ctx)) return false;
                progress=true;
            }
        }
        return true;
    }
};

class Decoder {
    struct Flow { bool used=false; uint16_t port=0; uint32_t dest=0; Stream stream; } flows[2];
    uint32_t seen[4]={};
    Sink output; void *context;
    static void accept(const Reading &r,void *ctx) {
        auto d=static_cast<Decoder*>(ctx);
        unsigned i=r.unit==6?0:r.unit==2?1:r.unit==3?2:3;
        if(r.stamp>d->seen[i]) { d->seen[i]=r.stamp; ++d->readings; if(d->output) d->output(r,d->context); }
    }
public:
    uint32_t packets=0,readings=0,resyncs=0,drops=0;
    explicit Decoder(Sink s=nullptr,void *c=nullptr):output(s),context(c) {}
    void disconnect() { for(auto &f:flows) { f.used=false; f.stream.reset(); } }
    void frame(const uint8_t *f,size_t n) {
        static const uint8_t mac[]={0xc0,0xbc,0x9a,0x1b,0x83,0xf0}, ip[]={192,168,1,123};
        if(n<54 || memcmp(f+6,mac,6)) return;
        size_t eth=14; unsigned proto=u16(f+12);
        if(proto==0x8100) { eth=18; proto=u16(f+16); }
        if(proto!=0x800 || n<eth+40) return;
        const auto b=f+eth; size_t ihl=(b[0]&15)*4,total=u16(b+2);
        if(b[0]>>4!=4 || b[9]!=6 || memcmp(b+12,ip,4) || ihl<20 || total>n-eth || total<ihl+20 || (u16(b+6)&0x3fff)) return;
        const auto tcp=b+ihl; size_t thl=(tcp[12]>>4)*4,tn=total-ihl;
        if(u16(tcp+2)!=27251 || thl<20 || thl>tn) return;
        ++packets;
        uint16_t port=u16(tcp); uint32_t dest=u32(b+16); Flow *flow=nullptr;
        for(auto &v:flows) if(v.used && v.port==port && v.dest==dest) flow=&v;
        if(!flow) {
            for(auto &v:flows) if(!v.used) { flow=&v; break; }
            if(!flow) { flow=&flows[0]; ++drops; }
            flow->used=true; flow->port=port; flow->dest=dest; flow->stream.reset();
        }
        bool syn=tcp[13]&2;
        if(syn) flow->stream.reset();
        uint32_t seq=u32(tcp+4)+syn; const auto p=tcp+thl; size_t len=tn-thl;
        auto &s=flow->stream;
        if(s.initialized && len>=19 && int32_t(seq-s.next)>0) {
            size_t alen=u16(p+4)+6;
            if(alen<=len && decode(p,alen,nullptr,nullptr)) { s.reset(); ++resyncs; }
        }
        if(!s.feed(seq,p,len,accept,this)) ++drops;
        if(tcp[13]&5) { flow->used=false; s.reset(); }
    }
    void tzsp(const uint8_t *p,size_t n) {
        if(n<5 || p[0]!=1 || p[1]!=0 || p[2]!=0 || p[3]!=1) return;
        size_t at=4;
        while(at<n) {
            auto tag=p[at++]; if(!tag) continue;
            if(tag==1) { frame(p+at,n-at); return; }
            if(at>=n) return;
            size_t size=p[at++]; if(size>n-at) return; at+=size;
        }
    }
};
}
