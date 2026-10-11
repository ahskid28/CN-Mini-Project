#include "clients.h"
static int checks=0, failures=0;
static void check(int ok,const char *name) { checks++; if(!ok) failures++; printf("%s %s\n",ok?"PASS":"FAIL",name); }
int main(int argc,char **argv) {
    Lab l; Response *r=malloc(sizeof(*r)); int i,j; char path[128],wire[8192]; double times[16]; Batch b;
    const char *paths[]={"/private","/cookie","/vary","/zero-ttl"};
    const char *headers[]={"Authorization: demo\r\n","Cookie: a=b\r\n","Range: bytes=0-1\r\n","Cache-Control: no-cache\r\n"};
    if(argc!=3) { fprintf(stderr,"Usage: %s PROXY_BINARY ORIGIN_BINARY\n",argv[0]); free(r); return 2; }
    if(!r || net_init() || !lab_start(&l,argv[1],argv[2],"--allow-private",20)) { free(r); return 2; }
#define REQ(p,h) request(l.proxy,l.origin,"GET",p,h,0,r)
#define STATUS(p,s) check(REQ(p,"") && r->status==s,p)
    check(REQ("/","") && r->status==200 && strstr((char *)r->data,"X-Proxy-Cache: MISS"),"cache MISS");
    check(REQ("/","") && strstr((char *)r->data,"X-Proxy-Cache: HIT"),"cache HIT");
    memset(&b,0,sizeof(b)); b.port=l.proxy; b.origin=l.origin; b.unique=1; b.count=16; b.times=times;
    check(batch_run(&b,8),"16 concurrent requests");
    for(i=0;i<2;i++) { snprintf(wire,sizeof(wire),"GET http://%s/ HTTP/1.0\r\n\r\n",i?"sub.blocked.example":"blocked.example"); check(exchange(l.proxy,wire,r) && r->status==403,"domain blocking"); }
    { const char *targets[]={"/relative","https://example.com/","http://u:p@example.com/","http://example.com:99999/"};
      for(i=0;i<4;i++) { snprintf(wire,sizeof(wire),"GET %s HTTP/1.0\r\n\r\n",targets[i]); check(exchange(l.proxy,wire,r) && r->status==400,"invalid target"); } }
    check(request(l.proxy,l.origin,"POST","/","",0,r) && r->status==405,"POST");
    check(exchange(l.proxy,"CONNECT example.com:443 HTTP/1.0\r\n\r\n",r) && r->status==501,"CONNECT");
    check(request(l.proxy,l.origin,"HEAD","/","",0,r) && r->status==200 && r->size==r->body && strstr((char *)r->data,"Content-Length: 22"),"HEAD");
    for(i=0;i<4;i++) check(REQ("/",headers[i]) && strstr((char *)r->data,"X-Proxy-Cache: BYPASS"),"cache bypass");
    for(i=0;i<4;i++) for(j=0;j<2;j++) check(REQ(paths[i],"") && strstr((char *)r->data,"X-Proxy-Cache: MISS"),paths[i]);
    REQ("/variant","Accept: text/plain\r\n");
    check(REQ("/variant","Accept: application/json\r\n") && strstr((char *)r->data,"X-Proxy-Cache: MISS"),"header variants");
    check(REQ("/hop","") && !strstr((char *)r->data,"X-Internal"),"hop headers removed");
    for(j=0;j<2;j++) { int ok=REQ("/binary","") && r->size-r->body==256; for(i=0;ok && i<256;i++) if(r->data[r->body+(size_t)i]!=(unsigned char)i) ok=0; check(ok,"binary body"); }
    check(REQ("/","Content-Length: 2\r\n") && r->status==400,"request body rejected");
    check(REQ("/","Content-Length: 0\r\nContent-Length: 0\r\n") && r->status==400,"duplicate framing");
    STATUS("/large",502); STATUS("/truncated",502); STATUS("/chunked",502); STATUS("/slow",504);
    snprintf(wire,sizeof(wire),"GET http://127.0.0.1:%d/ HTTP/1.0\r\n\r\n",free_port());
    check(exchange(l.proxy,wire,r) && r->status==502,"unreachable origin");
    REQ("/log","");
    { FILE *f=fopen(l.log,"r"); int ok=0; if(f) { while(fgets(wire,sizeof(wire),f)) if(strstr(wire,"127.0.0.1") && strstr(wire,"MISS") && strstr(wire,"200")) ok=1; fclose(f); } check(ok,"CSV log"); }
    for(i=0;i<17;i++) { snprintf(path,sizeof(path),"/?evict=%d",i); REQ(path,""); }
    check(REQ("/?evict=0","") && strstr((char *)r->data,"X-Proxy-Cache: MISS"),"LRU eviction");
    lab_close(&l);
    if(!lab_start(&l,argv[1],argv[2],"",20)) { free(r); return 2; }
    STATUS("/",403); lab_close(&l);
    if(!lab_start(&l,argv[1],argv[2],"--allow-private --rate 1 --window 1",20)) { free(r); return 2; }
    STATUS("/",200); STATUS("/",429); pause_ms(1100); STATUS("/",200); lab_close(&l);
    if(!lab_start(&l,argv[1],argv[2],"--allow-private --ttl 1",20)) { free(r); return 2; }
    REQ("/",""); pause_ms(1100);
    check(REQ("/","") && strstr((char *)r->data,"X-Proxy-Cache: MISS"),"TTL expiry");
    lab_close(&l); free(r); printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
}
