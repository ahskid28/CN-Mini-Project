#ifndef CLIENTS_H
#define CLIENTS_H
#include "lab.h"
typedef struct { int port,origin,direct,unique,count,next,hits,failures; double *times; mutex_t lock; } Batch;
static void run_client(Batch *b) {
    Response *r=malloc(sizeof(*r)); int i; char path[64];
    if(!r) { mutex_lock(&b->lock); b->failures++; mutex_unlock(&b->lock); return; }
    for(;;) {
        double start;
        mutex_lock(&b->lock); i=b->next++; mutex_unlock(&b->lock); if(i>=b->count) break;
        if(b->unique) snprintf(path,sizeof(path),"/?id=%d",i); else strcpy(path,"/");
        start=now_seconds();
        if(!request(b->port,b->origin,"GET",path,"",b->direct,r) || r->status!=200 || r->size<=r->body) { mutex_lock(&b->lock); b->failures++; mutex_unlock(&b->lock); }
        b->times[i]=(now_seconds()-start)*1000;
        if(strstr((char *)r->data,"X-Proxy-Cache: HIT")) { mutex_lock(&b->lock); b->hits++; mutex_unlock(&b->lock); }
    }
    free(r);
}
#ifdef _WIN32
static DWORD WINAPI client_worker(LPVOID p) { run_client(p); return 0; }
#else
static void *client_worker(void *p) { run_client(p); return NULL; }
#endif
static int batch_run(Batch *b,int concurrency) {
    int i,n=0;
#ifdef _WIN32
    HANDLE threads[32];
#else
    pthread_t threads[32];
#endif
    mutex_init(&b->lock);
    for(i=0;i<concurrency;i++) {
#ifdef _WIN32
        threads[n]=CreateThread(NULL,0,client_worker,b,0,NULL); if(!threads[n]) break;
#else
        if(pthread_create(&threads[n],NULL,client_worker,b)) break;
#endif
        n++;
    }
    for(i=0;i<n;i++) {
#ifdef _WIN32
        WaitForSingleObject(threads[i],INFINITE); CloseHandle(threads[i]);
#else
        pthread_join(threads[i],NULL);
#endif
    }
#ifdef _WIN32
    DeleteCriticalSection(&b->lock);
#else
    pthread_mutex_destroy(&b->lock);
#endif
    return n==concurrency && !b->failures;
}
#endif
