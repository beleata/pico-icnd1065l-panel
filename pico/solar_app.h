#pragma once
#include "solar_display.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "lwip/udp.h"
#include "solar_wifi.h"
#include "solar_web.h"
#include "solar_readings.h"

namespace solar_app {
inline solar::State state;
inline char identity[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1];
inline bool initialized=false, connected=false;
inline uint32_t last_stalls=0,last_errors=0;
inline bool readings_json(char *out,size_t capacity) {
    return solar_readings::format(out,capacity,state,time_us_64(),
        cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA)==CYW43_LINK_UP);
}
inline void reading(const solar::Reading &r,void *) {
    state.ingest(r,time_us_64());
    if(!stdio_usb_connected()) return;
    // Retain Solar's USB JSON contract as an optional diagnostic output.
    printf("{\"event\":\"reading\",\"pico_id\":\"%s\",\"unit\":%u,\"device_timestamp_raw\":%lu,\"kind\":\"%s\",\"pv_w\":%ld",
        identity,r.unit,(unsigned long)r.stamp,r.unit==100?"site":"inverter",(long)r.pv);
    if(r.unit==100) printf(",\"load_w\":%ld,\"grid_w\":%ld,\"battery_w\":%ld",(long)r.load,(long)r.grid,(long)r.battery);
    else {
        printf(",\"ac_w\":%ld,\"frequency_hz\":%.2f",(long)r.ac,r.hz10/100.0);
        if(r.unit==2 || r.unit==3) printf(",\"battery_soc_pct\":%.1f,\"battery_capacity_wh\":%lu,\"battery_w\":%ld",r.soc10/10.0,(unsigned long)r.capacity,(long)r.battery);
    }
    printf("}\n");
}
inline solar::Decoder decoder(reading,nullptr);
inline uint8_t datagram[4096];
inline void receive(void *,udp_pcb *,pbuf *p,const ip_addr_t *from,u16_t) {
    if(!p) return;
    ip_addr_t router; ipaddr_aton(SOLAR_ROUTER,&router);
    if(ip_addr_cmp(from,&router) && p->tot_len<=sizeof(datagram)) {
        size_t n=p->tot_len; pbuf_copy_partial(p,datagram,n,0); decoder.tzsp(datagram,n);
    }
    pbuf_free(p);
}
inline bool init() {
    pico_get_unique_board_id_string(identity,sizeof(identity));
    if(cyw43_arch_init()) return false;
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state,CYW43_NO_POWERSAVE_MODE);
    auto pcb=udp_new_ip_type(IPADDR_TYPE_V4);
    if(!pcb) return false;
    if(udp_bind(pcb,IP_ANY_TYPE,37008)!=ERR_OK) { udp_remove(pcb); return false; }
    udp_recv(pcb,receive,nullptr);
    solar_web::readings_provider=readings_json;
    initialized=solar_web::init(); return initialized;
}
inline void poll(uint32_t stalls,uint32_t errors) {
    last_stalls=stalls; last_errors=errors;
    if(!initialized) return;
    cyw43_arch_poll();
    uint64_t now=time_us_64();
    static uint64_t retry=0,status_at=0;
    int status=cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA);
    bool up=status==CYW43_LINK_UP;
    if(connected && !up) decoder.disconnect();
    connected=up;
    solar_web::poll(decoder.readings,errors|stalls);
    ota::poll(up && solar_web::listener && !errors && !stalls);
    if(!up && now>=retry) {
        cyw43_arch_wifi_connect_async(SOLAR_SSID,SOLAR_PASSWORD,CYW43_AUTH_WPA2_AES_PSK);
        retry=now+45000000;
    }
    if(now>=status_at) {
        if(stdio_usb_connected()) printf("{\"event\":\"status\",\"pico_id\":\"%s\",\"state\":\"%s\",\"ip\":\"%s\",\"packets\":%lu,\"readings\":%lu,\"resyncs\":%lu,\"drops\":%lu,\"txstall\":%lu,\"dma_error\":%lu}\n",
            identity,up?"listening":"connecting",netif_default?ip4addr_ntoa(netif_ip4_addr(netif_default)):"0.0.0.0",
            (unsigned long)decoder.packets,(unsigned long)decoder.readings,(unsigned long)decoder.resyncs,
            (unsigned long)decoder.drops,(unsigned long)stalls,(unsigned long)errors);
        status_at=now+10000000;
    }
}
inline void frame(uint8_t *target) {
    uint64_t now=time_us_64();
    solar::render(state,connected,now,target,ota::settings);
}
}
