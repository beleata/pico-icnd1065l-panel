#pragma once
#include "ota_flash.h"
#include "solar_settings.h"
#include "panel_admin.h"
#include "mbedtls/sha256.h"
#include "hardware/watchdog.h"
namespace ota {
inline void (*pause_hook)(bool)=nullptr;
inline bool active=false,trial=false,confirmed=false;
inline uint32_t expected=0,received=0;
inline uint64_t deadline=0,healthy_at=0,boot_at=0,reboot_at=0;
inline uint8_t expected_hash[32];
inline solar::Settings settings;
inline void hash(const void *data,size_t size,uint8_t out[32]) {
    mbedtls_sha256(static_cast<const unsigned char*>(data),size,out,0);
}
inline bool equal(const uint8_t*a,const uint8_t*b,size_t n) {
    uint8_t diff=0;while(n--)diff|=*a++^*b++;return diff==0;
}
inline void hmac_header(const uint8_t *h,uint8_t out[32]) {
    uint8_t key[64]={},inner[32];memcpy(key,PANEL_SIGNING_KEY,32);
    for(auto &b:key)b^=0x36;
    mbedtls_sha256_context c;mbedtls_sha256_init(&c);mbedtls_sha256_starts(&c,0);
    mbedtls_sha256_update(&c,key,64);mbedtls_sha256_update(&c,h,48);mbedtls_sha256_update(&c,h+80,48);
    mbedtls_sha256_finish(&c,inner);
    for(auto &b:key)b^=0x36^0x5c;
    mbedtls_sha256_starts(&c,0);mbedtls_sha256_update(&c,key,64);mbedtls_sha256_update(&c,inner,32);
    mbedtls_sha256_finish(&c,out);mbedtls_sha256_free(&c);
}
inline void pause(bool p) {if(pause_hook)pause_hook(p);}
inline void init() {
    trial=state(flash).mode==TRIAL;boot_at=time_us_64();
    Journal<Flash> j(flash,SETTINGS,0x31544553);Record r;
    if(j.latest(r)>=0) { solar::Settings saved;memcpy(&saved,r.payload,sizeof(saved));if(solar::valid_settings(saved))settings=saved; }
    watchdog_enable(8000,true);
}
inline bool set_settings(const uint8_t *body,size_t n) {
    if(active || n!=sizeof(settings))return false;
    solar::Settings candidate;memcpy(&candidate,body,n);
    if(!solar::valid_settings(candidate))return false;
    if(!memcmp(&candidate,&settings,n))return true;
    pause(true);Journal<Flash> j(flash,SETTINGS,0x31544553);
    bool ok=j.save(&candidate,n);if(ok)settings=candidate;pause(false);return ok;
}
inline void abort() { active=false;received=expected=0;pause(false); }
inline bool begin(const uint8_t *body,size_t n) {
    if(active || !confirmed || n!=128 || memcmp(body,"PNLOTA1\0",8))return false;
    uint32_t len,base;memcpy(&len,body+8,4);memcpy(&base,body+12,4);
    uint8_t signature[32];hmac_header(body,signature);
    if(base!=APP || len<256 || len>SLOT || len%256 || !equal(signature,body+48,32))return false;
    pause(true);auto s=state(flash);s.backup=0;
    if(s.mode!=IDLE || !save(flash,s)) {pause(false);return false;}
    memcpy(expected_hash,body+16,32);expected=len;received=0;active=true;
    deadline=time_us_64()+60000000;return true;
}
inline bool chunk(uint32_t offset,const uint8_t *body,size_t n) {
    if(!active || offset!=received || offset%SECTOR || n==0 || n>SECTOR || n%PAGE || offset+n>expected ||
        (n!=SECTOR && offset+n!=expected))return false;
    if(!flash.erase(DOWNLOAD+offset,SECTOR) || !flash.program(DOWNLOAD+offset,body,n) ||
       memcmp(flash.ptr(DOWNLOAD+offset),body,n)) {abort();return false;}
    received+=n;deadline=time_us_64()+60000000;return true;
}
inline bool finish() {
    if(!active || received!=expected)return false;
    uint8_t digest[32];hash(flash.ptr(DOWNLOAD),expected,digest);
    uint32_t sp,pc;memcpy(&sp,flash.ptr(DOWNLOAD),4);memcpy(&pc,flash.ptr(DOWNLOAD+4),4);
    if(!equal(digest,expected_hash,32) || sp<0x20000000 || sp>0x20042000 || !(pc&1) || pc<XIP_BASE+APP || pc>=XIP_BASE+APP+expected) {abort();return false;}
    BootState s;s.mode=UPDATE;
    if(!save(flash,s)) {abort();return false;}
    reboot_at=time_us_64()+1500000;return true;
}
inline bool rollback() {
    auto s=state(flash);if(active || s.mode!=IDLE || !s.backup)return false;
    pause(true);s.mode=ROLLBACK;s.index=s.phase=0;
    if(!save(flash,s)) {pause(false);return false;}
    active=true;reboot_at=time_us_64()+1500000;return true;
}
inline void poll(bool healthy) {
    watchdog_update();uint64_t now=time_us_64();
    if(reboot_at && now>=reboot_at) {watchdog_reboot(0,0,1);while(true)tight_loop_contents();}
    if(active && !reboot_at && now>deadline)abort();
    if(!confirmed) {
        if(healthy) {if(!healthy_at)healthy_at=now;} else healthy_at=0;
        if(healthy_at && now-healthy_at>10000000) {
            auto s=state(flash);s.mode=IDLE;
            pause(true);confirmed=save(flash,s);pause(false);
        }
        if(trial && now-boot_at>120000000)watchdog_reboot(0,0,1);
    }
}
}
