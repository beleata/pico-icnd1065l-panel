#pragma once
#include "solar_display.h"

namespace solar_readings {
constexpr size_t BUFFER_SIZE=384;
constexpr uint64_t MAX_AGE_US=120000000ull;
inline bool fresh(bool valid,uint64_t at,uint64_t now) {
    return valid && now>=at && now-at<MAX_AGE_US;
}
inline bool format(char *out,size_t capacity,const solar::State &s,uint64_t now,bool connected) {
    char stamp[16]="null",soc[16]="null",pv[16]="null",load[16]="null",grid[16]="null",battery[16]="null";
    if(s.site_valid) {
        snprintf(stamp,sizeof(stamp),"%lu",(unsigned long)s.site.stamp);
        snprintf(pv,sizeof(pv),"%ld",(long)s.site.pv);
        snprintf(load,sizeof(load),"%ld",(long)s.site.load);
        snprintf(grid,sizeof(grid),"%ld",(long)s.site.grid);
        snprintf(battery,sizeof(battery),"%ld",(long)s.site.battery);
    }
    if(s.battery_valid[0] && s.battery_valid[1]) {
        unsigned hundredths=(unsigned(s.batteries[0].soc10)+s.batteries[1].soc10)*5;
        snprintf(soc,sizeof(soc),"%u.%02u",hundredths/100,hundredths%100);
    }
    // ts identifies the site report, not request time; SOC has independent ages.
    bool stale=!connected || !fresh(s.site_valid,s.site_at,now) ||
        !fresh(s.battery_valid[0],s.battery_at[0],now) ||
        !fresh(s.battery_valid[1],s.battery_at[1],now);
    int n=snprintf(out,capacity,"{\"ts\":%s,\"stale\":%s,\"solar_power\":%s,\"battery_perc\":%s,\"battery_power\":%s,\"grid_power\":%s,\"consume_power\":%s}",stamp,stale?"true":"false",pv,soc,battery,grid,load);
    return n>=0 && size_t(n)<capacity;
}
}
