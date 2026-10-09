#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <limits.h>
#include "solar_readings.h"
int main() {
    solar::State s;
    char out[solar_readings::BUFFER_SIZE];
    auto render=[&](uint64_t now,bool up=true) { assert(solar_readings::format(out,sizeof(out),s,now,up)); };
    render(0);
    assert(!strcmp(out,"{\"ts\":null,\"stale\":true,\"solar_power\":null,\"battery_perc\":null,\"battery_power\":null,\"grid_power\":null,\"consume_power\":null}"));
    solar::Reading r; r.unit=100;r.stamp=1760000000;r.pv=4210;r.load=1160;r.grid=-3050;r.battery=-1200;
    s.ingest(r,1000);render(1000);
    assert(strstr(out,"\"battery_perc\":null"));
    assert(strstr(out,"\"solar_power\":4210"));
    r.unit=2;r.soc10=780;s.ingest(r,1000);render(1000);
    assert(strstr(out,"\"battery_perc\":null"));
    r.unit=3;r.soc10=781;s.ingest(r,1000);render(1000);
    assert(strstr(out,"\"battery_perc\":78.05"));
    assert(strstr(out,"\"stale\":false"));
    render(120000999);assert(strstr(out,"\"stale\":false"));
    render(120001000);assert(strstr(out,"\"stale\":true"));
    assert(strstr(out,"\"grid_power\":-3050"));
    render(1000,false);assert(strstr(out,"\"stale\":true"));
    s.battery_at[0]=0;render(120000000);assert(strstr(out,"\"stale\":true"));
    s.site.pv=INT32_MAX;s.site.load=INT32_MIN;s.site.grid=INT32_MIN;s.site.battery=INT32_MIN;s.site.stamp=UINT32_MAX;
    render(1000);assert(strlen(out)<sizeof(out));
    char tiny[2];assert(!solar_readings::format(tiny,sizeof(tiny),s,1000,true));
    puts("Readings cache, nulls, signs, mean, expiry and bounds: PASS");
}
