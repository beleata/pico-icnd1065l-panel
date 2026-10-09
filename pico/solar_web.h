#pragma once
#include "ota_runtime.h"
#include "panel_web_page.h"
#include "lwip/tcp.h"
#include <strings.h>
#include <stdlib.h>
#ifdef PICO_PANEL_CHAIN_ARROW
#include "chain_arrow.h"
#endif
#ifndef PANEL_VERSION
#define PANEL_VERSION "0.4.2-solar-readings"
#endif
namespace solar_web {
inline tcp_pcb *listener=nullptr,*client=nullptr;
inline char input[5376],output[1024],headers[256];
inline char readings_output[384];
inline bool (*readings_provider)(char*,size_t)=nullptr;
inline size_t used=0,header_size=0,content_length=0,sent_header=0,sent_body=0,body_size=0;
inline const char *body=nullptr;
inline uint64_t touched=0;
inline bool responding=false;
inline uint32_t reports=0,errors=0;
inline void clear() {client=nullptr;used=header_size=content_length=sent_header=sent_body=body_size=0;body=nullptr;responding=false;}
inline void error(void*,err_t) {clear();}
inline void drop() {if(client){tcp_arg(client,nullptr);tcp_err(client,nullptr);tcp_abort(client);}clear();}
inline void pump() {
    if(!client || !responding)return;
    size_t n=strlen(headers);
    while(sent_header<n || sent_body<body_size) {
        bool h=sent_header<n;size_t remaining=h?n-sent_header:body_size-sent_body;
        size_t amount=remaining<tcp_sndbuf(client)?remaining:tcp_sndbuf(client);
        if(!amount)break;
        if(tcp_write(client,h?headers+sent_header:body+sent_body,amount,TCP_WRITE_FLAG_COPY)!=ERR_OK)break;
        if(h)sent_header+=amount;else sent_body+=amount;
    }
    tcp_output(client);
}
inline err_t ack(void*,tcp_pcb*p,u16_t) {
    touched=time_us_64();pump();
    if(sent_header==strlen(headers) && sent_body==body_size && tcp_sndbuf(p)==TCP_SND_BUF) {
        tcp_recv(p,nullptr);tcp_sent(p,nullptr);tcp_err(p,nullptr);
        if(tcp_close(p)!=ERR_OK){tcp_abort(p);clear();return ERR_ABRT;}
        clear();
    }
    return ERR_OK;
}
inline void response(int code,const char *text,const char *type="application/json") {
    body=text;body_size=strlen(text);responding=true;
    snprintf(headers,sizeof(headers),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",code,code==200?"OK":"Error",type,unsigned(body_size));
    pump();
}
inline bool header_value(const char *name,char *out,size_t cap) {
    const char *p=strstr(input,"\r\n");size_t n=strlen(name);
    if(!p)return false;p+=2;
    while(p<input+header_size-2) {
        const char *end=strstr(p,"\r\n");if(!end)return false;
        if(size_t(end-p)>n && !strncasecmp(p,name,n) && p[n]==':') {
            const char *value=p+n+1;while(value<end && *value==' ')++value;
            size_t len=end-value;if(len>=cap)return false;memcpy(out,value,len);out[len]=0;return true;
        }
        p=end+2;
    }
    return false;
}
inline void dispatch() {
    char method[8],path[96],auth[96];
    if(sscanf(input,"%7s %95s",method,path)!=2){response(400,"Bad request","text/plain");return;}
    bool get=!strcmp(method,"GET"),post=!strcmp(method,"POST");
    if(get && !strcmp(path,"/")) {response(200,PANEL_WEB_PAGE,"text/html; charset=utf-8");return;}
    if(get && !strcmp(path,"/api/status")) {
        auto s=ota::state(ota::flash);
        snprintf(output,sizeof(output),"{\"version\":\"%s\",\"ip\":\"%s\",\"confirmed\":%s,\"backup\":%s,\"updating\":%s,\"readings\":%lu,\"errors\":%lu}",PANEL_VERSION,netif_default?ip4addr_ntoa(netif_ip4_addr(netif_default)):"0.0.0.0",ota::confirmed?"true":"false",s.backup?"true":"false",ota::active?"true":"false",(unsigned long)reports,(unsigned long)errors);
        response(200,output);return;
    }
    const char expected_auth[]="Bearer " PANEL_ADMIN_TOKEN;
    if(!header_value("Authorization",auth,sizeof(auth)) || strlen(auth)!=sizeof(expected_auth)-1 ||
       !ota::equal(reinterpret_cast<const uint8_t*>(auth),reinterpret_cast<const uint8_t*>(expected_auth),sizeof(expected_auth)-1)) {
        response(401,"Invalid management key","text/plain");return;
    }
    if(get && !strcmp(path,"/api/readings")) {
        if(!readings_provider || !readings_provider(readings_output,sizeof(readings_output))) {
            response(503,"Readings unavailable","text/plain");return;
        }
        response(200,readings_output);return;
    }
    if(get && !strcmp(path,"/api/settings")) {
        auto&s=ota::settings;
        snprintf(output,sizeof(output),"{\"version\":1,\"brightness\":%lu,\"load\":%lu,\"export_color\":%lu,\"import_color\":%lu,\"swap_lower\":%lu,\"rotate\":%lu}",(unsigned long)s.brightness,(unsigned long)s.load,(unsigned long)s.export_color,(unsigned long)s.import_color,(unsigned long)s.swap_lower,(unsigned long)s.rotate);
        response(200,output);return;
    }
    if(!post) {response(404,"Not found","text/plain");return;}
#ifdef PICO_PANEL_CHAIN_ARROW
    if(!strcmp(path,"/api/test/arrow") && !content_length) {
        chain_arrow::start();response(200,"{\"ok\":true}");return;
    }
#endif
    auto bytes=reinterpret_cast<uint8_t*>(input+header_size);bool ok=false;
    if(!strcmp(path,"/api/settings"))ok=ota::set_settings(bytes,content_length);
    else if(!strcmp(path,"/api/ota/begin"))ok=ota::begin(bytes,content_length);
    else if(!strncmp(path,"/api/ota/chunk?offset=",22)) {
        char *end;unsigned long offset=strtoul(path+22,&end,10);
        if(*end==0 && end!=path+22 && offset<=ota::SLOT)ok=ota::chunk(offset,bytes,content_length);
    }
    else if(!strcmp(path,"/api/ota/finish") && !content_length)ok=ota::finish();
    else if(!strcmp(path,"/api/ota/abort") && !content_length && !ota::reboot_at) {ota::abort();ok=true;}
    else if(!strcmp(path,"/api/ota/rollback") && !content_length)ok=ota::rollback();
    response(ok?200:409,ok?"{\"ok\":true}":"Request rejected: state, size, signature or data invalid",ok?"application/json":"text/plain");
}
inline err_t receive(void*,tcp_pcb*p,pbuf *buf,err_t err) {
    if(!buf){drop();return ERR_ABRT;}
    if(err!=ERR_OK || responding || used+buf->tot_len>=sizeof(input)) {pbuf_free(buf);drop();return ERR_ABRT;}
    size_t n=buf->tot_len;pbuf_copy_partial(buf,input+used,n,0);used+=n;input[used]=0;
    tcp_recved(p,n);pbuf_free(buf);touched=time_us_64();
    if(!header_size) {
        char *end=strstr(input,"\r\n\r\n");
        if(!end){if(used>1024){response(400,"Headers too large","text/plain");}return ERR_OK;}
        header_size=end-input+4;
        char value[40];
        if(header_size>1024 || header_value("Transfer-Encoding",value,sizeof(value))) {response(400,"Invalid headers","text/plain");return ERR_OK;}
        if(header_value("Content-Length",value,sizeof(value))) {
            char *last;unsigned long len=strtoul(value,&last,10);
            if(*last || last==value || len>4096) {response(400,"Invalid length","text/plain");return ERR_OK;}
            content_length=len;
        }
    }
    if(used>=header_size+content_length) {
        if(used!=header_size+content_length)response(400,"Extra data","text/plain");else dispatch();
    }
    return ERR_OK;
}
inline err_t accept(void*,tcp_pcb*p,err_t err) {
    if(err!=ERR_OK)return err;
    if(client) {tcp_abort(p);return ERR_ABRT;}
    clear();client=p;touched=time_us_64();tcp_recv(p,receive);tcp_sent(p,ack);tcp_err(p,error);return ERR_OK;
}
inline bool init() {
    auto p=tcp_new_ip_type(IPADDR_TYPE_V4);if(!p)return false;
    if(tcp_bind(p,IP_ANY_TYPE,80)!=ERR_OK){tcp_abort(p);return false;}
    listener=tcp_listen_with_backlog(p,1);if(!listener){tcp_abort(p);return false;}
    tcp_accept(listener,accept);return true;
}
inline void poll(uint32_t count,uint32_t faults) {
    reports=count;errors=faults;
    if(client && time_us_64()-touched>15000000)drop();else pump();
}
}
