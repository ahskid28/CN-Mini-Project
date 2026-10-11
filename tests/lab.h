#ifndef LAB_H
#define LAB_H
#include "../src/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#endif
typedef struct { unsigned char data[1200000]; size_t size, body; int status; } Response;
#ifdef _WIN32
typedef HANDLE process_t;
#else
typedef pid_t process_t;
#endif
static int net_init(void) {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2,2), &w);
#else
    signal(SIGPIPE, SIG_IGN);
    return 0;
#endif
}
static socket_t connect_local(int port) {
    struct sockaddr_in a;
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((unsigned short)port);
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    socket_timeout(s,5);
    if(connect(s,(struct sockaddr *)&a,sizeof(a)) != 0) { close_socket(s); return BAD_SOCKET; }
    return s;
}
static int send_all(socket_t s, const void *data, size_t n) {
    const char *p=data;
    while(n) { int k=send(s,p,(int)n,0); if(k<=0) return 0; p+=k; n-=(size_t)k; }
    return 1;
}
static int exchange(int port, const char *wire, Response *r) {
    socket_t s=connect_local(port); int n; char *end;
    memset(r,0,sizeof(*r));
    if(s==BAD_SOCKET) return 0;
    if(!send_all(s,wire,strlen(wire))) { close_socket(s); return 0; }
    while(r->size<sizeof(r->data)-1 && (n=recv(s,(char *)r->data+r->size,(int)(sizeof(r->data)-1-r->size),0))>0) r->size+=(size_t)n;
    close_socket(s); r->data[r->size]=0;
    end=strstr((char *)r->data,"\r\n\r\n"); if(!end) return 0;
    r->body=(size_t)(end-(char *)r->data)+4;
    return sscanf((char *)r->data,"HTTP/%*s %d",&r->status)==1;
}
static int request(int port, int origin, const char *method, const char *path, const char *headers, int direct, Response *r) {
    char wire[8192], target[4096];
    if(direct) snprintf(target,sizeof(target),"%s",path);
    else snprintf(target,sizeof(target),"http://127.0.0.1:%d%s",origin,path);
    snprintf(wire,sizeof(wire),"%s %s HTTP/1.0\r\nHost: 127.0.0.1\r\n%s\r\n",method,target,headers);
    return exchange(port,wire,r);
}
static int free_port(void) {
    struct sockaddr_in a; socket_t s=socket(AF_INET,SOCK_STREAM,0);
#ifdef _WIN32
    int len=sizeof(a);
#else
    socklen_t len=sizeof(a);
#endif
    int port;
    memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(s,(struct sockaddr *)&a,sizeof(a)) || getsockname(s,(struct sockaddr *)&a,&len)) { close_socket(s); return 0; }
    port=ntohs(a.sin_port); close_socket(s); return port;
}
static process_t launch(const char *binary, const char *args) {
#ifdef _WIN32
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[8192];
    memset(&si,0,sizeof(si)); si.cb=sizeof(si);
    snprintf(cmd,sizeof(cmd),"\"%s\" %s",binary,args);
    if(!CreateProcessA(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) return NULL;
    CloseHandle(pi.hThread); return pi.hProcess;
#else
    pid_t p=fork();
    if(p==0) {
        char copy[8192], *argv[64], *token; int n=1;
        snprintf(copy,sizeof(copy),"%s",args); argv[0]=(char *)binary;
        token=strtok(copy," ");
        while(token && n<63) { argv[n++]=token; token=strtok(NULL," "); }
        argv[n]=NULL; execv(binary,argv); _exit(127);
    }
    return p;
#endif
}
static void stop_process(process_t p) {
#ifdef _WIN32
    if(p) { TerminateProcess(p,0); WaitForSingleObject(p,8000); CloseHandle(p); }
#else
    if(p>0) { kill(p,SIGTERM); waitpid(p,NULL,0); }
#endif
}
static int wait_ready(int port) {
    int i;
    for(i=0;i<100;i++) { socket_t s=connect_local(port); if(s!=BAD_SOCKET) { close_socket(s); return 1; } pause_ms(50); }
    return 0;
}
typedef struct { int origin, proxy; process_t op, pp; char log[128]; } Lab;
static void lab_close(Lab *l) { stop_process(l->pp); stop_process(l->op); remove(l->log); }
static int lab_start(Lab *l,const char *proxy,const char *origin,const char *options,int delay) {
    char args[2048]; memset(l,0,sizeof(*l)); l->origin=free_port(); l->proxy=free_port();
    snprintf(l->log,sizeof(l->log),"lab-%d.csv",l->proxy);
    snprintf(args,sizeof(args),"--port %d --delay %d",l->origin,delay); l->op=launch(origin,args);
    if(!wait_ready(l->origin)) { lab_close(l); return 0; }
    snprintf(args,sizeof(args),"--port %d --rate 0 --timeout 1 --log %s %s",l->proxy,l->log,options); l->pp=launch(proxy,args);
    if(!wait_ready(l->proxy)) { lab_close(l); return 0; } return 1;
}
#endif
