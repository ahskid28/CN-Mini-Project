#include "../src/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#endif
static int delay_ms=20;
static void handle(socket_t s) {
    char buf[16384], path[8192]="", method[16]="", header[2048]; size_t used=0; int n, head, binary, large;
    const char *control="public, max-age=30", *extra="";
    unsigned char body[256]; size_t size=22, i;
    socket_timeout(s,5);
    while(used<sizeof(buf)-1) { n=recv(s,buf+used,(int)(sizeof(buf)-1-used),0); if(n<=0) goto done; used+=(size_t)n; buf[used]=0; if(strstr(buf,"\r\n\r\n")) break; }
    if(sscanf(buf,"%15s %8191s",method,path)!=2) goto done;
    head=strcmp(method,"HEAD")==0; binary=strcmp(path,"/binary")==0; large=strcmp(path,"/large")==0;
    pause_ms(strncmp(path,"/slow",5)==0 ? 2000 : delay_ms);
    if(strcmp(path,"/chunked")==0) { const char *c="HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\ndemo\r\n0\r\n\r\n"; send(s,c,(int)strlen(c),0); goto done; }
    if(strcmp(path,"/private")==0) control="no-store";
    if(strcmp(path,"/zero-ttl")==0) control="public, max-age = \"0\"";
    if(strcmp(path,"/cookie")==0) extra="Set-Cookie: demo=1\r\n";
    if(strcmp(path,"/vary")==0) extra="Vary: Accept\r\n";
    if(strcmp(path,"/hop")==0) extra="Connection: X-Internal\r\nX-Internal: remove\r\n";
    if(binary) { size=256; for(i=0;i<size;i++) body[i]=(unsigned char)i; }
    else memcpy(body,"C proxy demonstration\n",22);
    if(large) size=1024*1024+1;
    n=snprintf(header,sizeof(header),"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\nCache-Control: %s\r\n%s\r\n",(unsigned long)(size+(strcmp(path,"/truncated")==0?10:0)),control,extra);
    if(send(s,header,n,0)<=0 || head) goto done;
    if(large) { memset(body,'x',sizeof(body)); while(size) { size_t count=size>sizeof(body)?sizeof(body):size; n=send(s,(char *)body,(int)count,0); if(n<=0) break; size-=(size_t)n; } }
    else { i=0; while(i<size) { n=send(s,(char *)body+i,(int)(size-i),0); if(n<=0) break; i+=(size_t)n; } }
done: close_socket(s);
}
#ifdef _WIN32
static DWORD WINAPI worker(LPVOID arg) { socket_t s=*(socket_t *)arg; free(arg); handle(s); return 0; }
#else
static void *worker(void *arg) { socket_t s=*(socket_t *)arg; free(arg); handle(s); return NULL; }
#endif
int main(int argc,char **argv) {
    struct sockaddr_in a; socket_t listener; int port=8000,i;
#ifdef _WIN32
    WSADATA w; if(WSAStartup(MAKEWORD(2,2),&w)) return 1;
#else
    signal(SIGPIPE,SIG_IGN);
#endif
    for(i=1;i<argc;i++) {
        if(i+1<argc && strcmp(argv[i],"--port")==0) port=atoi(argv[++i]);
        else if(i+1<argc && strcmp(argv[i],"--delay")==0) delay_ms=atoi(argv[++i]);
        else { fprintf(stderr,"Usage: %s [--port 8000] [--delay 20]\n",argv[0]); return 1; }
    }
    if(port<1 || port>65535 || delay_ms<0) return 1;
    /* Exercise the monotonic adapter used by the companion clients. */
    (void)now_seconds();
    listener=socket(AF_INET,SOCK_STREAM,0); memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((unsigned short)port); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(listener==BAD_SOCKET || bind(listener,(struct sockaddr *)&a,sizeof(a)) || listen(listener,64)) { fprintf(stderr,"Origin bind failed\n"); return 1; }
    printf("C test origin on 127.0.0.1:%d\n",port); fflush(stdout);
    for(;;) {
        socket_t s=accept(listener,NULL,NULL), *arg; if(s==BAD_SOCKET) break;
        arg=malloc(sizeof(*arg)); if(!arg) { close_socket(s); continue; } *arg=s;
#ifdef _WIN32
        { HANDLE t=CreateThread(NULL,0,worker,arg,0,NULL); if(t) CloseHandle(t); else { free(arg); close_socket(s); } }
#else
        { pthread_t t; if(pthread_create(&t,NULL,worker,arg)==0) pthread_detach(t); else { free(arg); close_socket(s); } }
#endif
    }
    close_socket(listener); return 0;
}
