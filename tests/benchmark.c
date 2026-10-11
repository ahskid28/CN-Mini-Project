#include "clients.h"
static int compare(const void *a,const void *b) { double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y); }
int main(int argc,char **argv) {
    Lab l; Batch b; double times[60]; int mode,i; FILE *out; Response *r;
    const char *names[]={"direct","proxy_cold","proxy_warm"};
    if(argc<3 || argc>4) { fprintf(stderr,"Usage: %s PROXY_BINARY ORIGIN_BINARY [OUTPUT_JSON]\n",argv[0]); return 2; }
    if(net_init() || !lab_start(&l,argv[1],argv[2],"--allow-private",50)) return 2;
    out=fopen(argc==4?argv[3]:"benchmark-results.json","w");
    if(!out) { lab_close(&l); return 2; }
    fprintf(out,"{\n  \"requests\": 60, \"concurrency\": 6, \"origin_delay_ms\": 50,\n  \"results\": [\n");
    for(mode=0;mode<3;mode++) {
        double start,elapsed,mean=0;
        if(mode==2) { r=malloc(sizeof(*r)); if(!r || !request(l.proxy,l.origin,"GET","/","",0,r) || r->status!=200) { free(r); fclose(out); lab_close(&l); return 1; } free(r); }
        memset(&b,0,sizeof(b)); b.port=mode==0?l.origin:l.proxy; b.origin=l.origin; b.direct=mode==0; b.unique=mode==1; b.count=60; b.times=times;
        start=now_seconds();
        if(!batch_run(&b,6)) { fclose(out); lab_close(&l); return 1; }
        elapsed=now_seconds()-start; for(i=0;i<60;i++) mean+=times[i]; mean/=60; qsort(times,60,sizeof(double),compare);
        fprintf(out,"    {\"mode\": \"%s\", \"mean_ms\": %.3f, \"p95_ms\": %.3f, \"requests_per_second\": %.3f, \"cache_hits\": %d}%s\n",names[mode],mean,times[56],60/elapsed,b.hits,mode==2?"":",");
        printf("%s: mean %.3f ms, p95 %.3f ms, %.3f req/s, %d hits\n",names[mode],mean,times[56],60/elapsed,b.hits);
        if((mode==2 && b.hits!=60) || (mode!=2 && b.hits!=0)) { fclose(out); lab_close(&l); return 1; }
    }
    fprintf(out,"  ]\n}\n"); fclose(out); lab_close(&l); return 0;
}
