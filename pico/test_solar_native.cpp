#include "solar_display.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static unsigned records=0;
static void collect(const solar::Reading &,void*) { ++records; }
static void emit(const solar::Reading &r,void*) {
    printf("{\"unit\":%u,\"stamp\":%u,\"pv\":%d,\"load\":%d,\"grid\":%d,\"battery\":%d,\"soc10\":%u}\n",r.unit,r.stamp,r.pv,r.load,r.grid,r.battery,r.soc10);
}
int main(int argc,char **argv) {
    if(argc<3) return 2;
    const char *mode=argv[1];
    if(!strcmp(mode,"frames")) {
        FILE *in=fopen(argv[2],"rb"); assert(in); solar::Decoder decoder(emit,nullptr);
        uint32_t n;
        static uint8_t frame[65536];
        while(fread(&n,4,1,in)==1) {
            if(n>65536) return 3;
            if(fread(frame,1,n,in)!=n) return 4;
            decoder.frame(frame,n);
        }
        fclose(in);
        return 0;
    }
    if(!strcmp(mode,"malformed")) {
        FILE *in=fopen(argv[2],"rb"); assert(in); uint32_t n; static uint8_t f[65536];
        while(fread(&n,4,1,in)==1) {
            assert(n<=sizeof(f) && fread(f,1,n,in)==n);
            for(size_t k=0;k<54 && k<n;++k) {
                static solar::Decoder d(collect,nullptr); records=0; d.frame(f,k); assert(!records);
            }
            if(n<54) continue;
            static uint8_t copy[65536]; memcpy(copy,f,n); memset(copy+6,0,6);
            static solar::Decoder d(collect,nullptr); records=0; d.frame(copy,n); assert(!records);
            memcpy(copy,f,n); copy[20]=0x20; copy[21]=0; d.frame(copy,n); assert(!records);
        }
        fclose(in);
        puts("PASS truncated, wrong source and fragmented packets rejected");
        return 0;
    }
    if(!strcmp(mode,"stream")) {
        FILE *in=fopen(argv[2],"rb"); assert(in);
        uint8_t a[8192]; size_t size=fread(a,1,sizeof(a),in); fclose(in);
        solar::decode(a,size,collect,nullptr); unsigned expected=records;
        assert(expected);
        for(size_t split=1;split<size;++split) {
            records=0; solar::Stream s;
            assert(s.feed(0xfffffff0,a,split,collect,nullptr));
            assert(s.feed(uint32_t(0xfffffff0+split),a+split,size-split,collect,nullptr));
            assert(records==expected);
            assert(s.feed(0xfffffff0,a,size,collect,nullptr));
            assert(records==expected);
        }
        records=0; solar::Stream s;
        s.feed(100,a,30,collect,nullptr);
        s.feed(200,a+100,size-100,collect,nullptr);
        s.feed(120,a+20,90,collect,nullptr);
        assert(records==expected);
        for(size_t n=0;n<size;++n) assert(!solar::decode(a,n,nullptr,nullptr));
        // Every panel coordinate maps to one and only one physical RGB pixel.
        uint8_t frame[video::FRAME_BYTES]={};
        for(int y=0;y<32;++y) for(int x=0;x<64;++x) solar::pixel(frame,x,y,{1,2,3});
        for(unsigned lane=0;lane<6;++lane) for(unsigned w=0;w<1024;++w) assert(frame[lane*1024+w]==lane%3+1);
        assert(!solar::stale(false,0,700000000));
        assert(!solar::stale(true,1,600000000));
        assert(solar::stale(true,1,600000001));
        assert(solar::whole_kw(0)==0 && solar::whole_kw(499)==0);
        assert(solar::whole_kw(500)==1 && solar::whole_kw(-1499)==1);
        assert(solar::whole_kw(18550)==19 && solar::whole_kw(-11992)==12);
        assert(solar::whole_kw(199999)==200);
        memset(frame,0,sizeof(frame));
        solar::number(frame,200,true,false,"KW",7,0,solar::YELLOW);
        for(unsigned plane=3;plane<6;++plane) for(unsigned w=0;w<1024;++w) assert(!frame[plane*1024+w]);
        memset(frame,0,sizeof(frame));
        solar::number(frame,100,true,false,"%",7,16,solar::GREEN);
        for(unsigned plane=0;plane<3;++plane) for(unsigned w=0;w<1024;++w) assert(!frame[plane*1024+w]);
        solar::State display; uint8_t other[video::FRAME_BYTES];
        solar::render(display,true,1,frame); solar::render(display,true,3000001,other);
        assert(!memcmp(frame,other,sizeof(frame))); // No page switching.
        display.batteries[0].soc10=920; display.batteries[1].soc10=1000;
        assert(solar::average_soc(display)==96);
        solar::Reading battery_report; battery_report.unit=100; battery_report.battery=1000;
        display.ingest(battery_report,1);
        assert(solar::battery_color(display,true,true)==solar::GREEN);
        battery_report.battery=-1; display.ingest(battery_report,2);
        assert(solar::battery_color(display,true,true)==solar::RED);
        display.batteries[0].soc10=1000;
        battery_report.battery=-10000; display.ingest(battery_report,3);
        assert(solar::battery_color(display,true,true)==solar::GREEN);
        assert(solar::battery_color(display,true,false)==solar::GREEN);
        assert(solar::battery_color(display,false,true)==solar::UNKNOWN);
        display.batteries[0].soc10=999;
        assert(solar::average_soc(display)==100);
        assert(solar::battery_color(display,true,true)==solar::GREEN);
        display.batteries[0].soc10=980;
        assert(solar::average_soc(display)==99);
        assert(solar::battery_color(display,true,true)==solar::RED);
        assert(solar::battery_color(display,true,false)==solar::UNKNOWN);
        assert(solar::charge_icon_color(display,true)==solar::GREEN);
        display.batteries[0].soc10=99; display.batteries[1].soc10=100;
        battery_report.battery=1000; display.ingest(battery_report,600000008);
        assert(solar::average_soc(display)==10);
        assert(solar::battery_color(display,true,true)==solar::RED); // Raw mean <10%, before rounding.
        assert(solar::charge_icon_color(display,true)==solar::RED);
        display.batteries[0].soc10=100;
        assert(solar::battery_color(display,true,true)==solar::GREEN);
        assert(solar::charge_icon_color(display,true)==solar::YELLOW);
        display.batteries[0].soc10=200; display.batteries[1].soc10=200;
        assert(solar::charge_icon_color(display,true)==solar::GREEN);
        assert(solar::charge_icon_color(display,false)==solar::UNKNOWN);
        assert(solar::battery_color(display,false,true)==solar::UNKNOWN);
        solar::Reading flow; flow.load=10000; flow.pv=12000;
        assert(solar::pv_color(flow)==solar::GREEN);
        flow.pv=10000; assert(solar::pv_color(flow)==solar::GREEN);
        flow.pv=8000; flow.battery=-2000;
        assert(solar::pv_color(flow)==solar::YELLOW);
        flow.grid=1; assert(solar::pv_color(flow)==solar::RED);
        flow.grid=0; flow.battery=0; assert(solar::pv_color(flow)==solar::UNKNOWN);
        assert(solar::grid_color(-1)==solar::YELLOW && solar::grid_color(1)==solar::RED);
        assert(solar::grid_color(0)==solar::WHITE);
        auto color_at=[&](int x,int y) {
            unsigned col=(unsigned(x)/32)*64+(y%16<8?32:0)+unsigned(x)%32;
            unsigned word=((unsigned(y)%8)*16+col%16)*8+col/16;
            unsigned base=(unsigned(y)/16)*3*1024+word;
            return solar::Color{frame[base],frame[base+1024],frame[base+2048]};
        };
        solar::State layout;
        solar::Reading sample; sample.unit=100; sample.load=2000; sample.grid=-18000;
        layout.ingest(sample,1);
        sample.unit=2; sample.soc10=1000; layout.ingest(sample,1);
        sample.unit=3; layout.ingest(sample,1);
        solar::render(layout,true,2,frame);
        assert(color_at(9,17)==solar::BLUE); // Building 2, bottom left.
        for(int y=16;y<32;++y) assert(color_at(7,y)==(solar::Color{0,0,0}));
        assert(color_at(41,17)==solar::YELLOW); // Grid 18, bottom right.
        assert(color_at(14,23)==solar::WHITE); // Building kW.
        assert(color_at(51,23)==solar::WHITE); // Grid kW.
        assert(color_at(45,7)==solar::WHITE); // PV kW after zero.
        assert(color_at(25,2)==solar::WHITE); // Percent sign after 100.
        assert(color_at(3,20)==solar::GREEN); // House stays green.
        assert(color_at(34,20)==solar::RED); // Lightning stays red.
        solar::Settings options;
        options.swap_lower=1;options.load=0x123456;
        solar::render(layout,true,2,frame,options);
        assert(color_at(41,17)==(solar::Color{0x12,0x34,0x56}));
        assert(color_at(9,17)==solar::YELLOW);
        options.brightness=50;
        solar::render(layout,true,2,frame,options);
        assert(color_at(41,17)==(solar::Color{9,26,43}));
        memcpy(other,frame,sizeof(frame));options.rotate=1;
        solar::render(layout,true,2,frame,options);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)for(unsigned c=0;c<3;++c)
            assert(frame[solar::pixel_offset(x,y)+c*1024]==other[solar::pixel_offset(63-x,31-y)+c*1024]);
        assert(solar::valid_settings(options));options.brightness=101;
        assert(!solar::valid_settings(options));
        for(char c='0';c<='9';++c) assert(strlen(solar::digit7(c))==35);
        for(char c='0';c<='9';++c) assert(strlen(solar::digit4(c))==28);
        // Maximum values and their units must stay in the left 32x16 cell.
        for(unsigned kind=0;kind<2;++kind) {
            memset(frame,0,sizeof(frame));
            solar::number(frame,kind?200:100,true,false,kind?"KW":"%",7,0,solar::WHITE);
            for(int y=0;y<32;++y) for(int x=0;x<64;++x) if(x>=32 || y>=16) {
                uint8_t probe[video::FRAME_BYTES]={}; solar::pixel(probe,x,y,{1,1,1});
                for(unsigned b=0;b<video::FRAME_BYTES;++b) if(probe[b]) assert(!frame[b]);
            }
        }
        puts("PASS native splits, overlap, duplicates, wraparound, bounds, mapping, stale");
        return 0;
    }
    if(!strcmp(mode,"render")) {
        solar::State state;
        if(argc>3 && !strcmp(argv[3],"sample")) {
            solar::Reading r; r.unit=100; r.pv=50000;r.load=30000;r.grid=-15000;r.battery=5000; state.ingest(r,1);
            r.unit=2;r.soc10=1000;state.ingest(r,1);r.unit=3;r.soc10=1000;state.ingest(r,1);
        }
        uint8_t frame[video::FRAME_BYTES]; FILE *out=fopen(argv[2],"wb"); assert(out);
        for(unsigned scenario=0;scenario<3;++scenario) {
            if(scenario==1) { state.site.pv=20000;state.site.battery=-10000;state.site.grid=0; }
            if(scenario==2) { state.site.pv=10000;state.site.battery=-10000;state.site.grid=10000; }
            state.ingest(state.site,1);
            solar::render(state,true,1000000,frame); fwrite(frame,1,sizeof(frame),out);
        }
        fclose(out);
        return 0;
    }
    return 2;
}
