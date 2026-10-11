#ifndef PLATFORM_H
#define PLATFORM_H
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
typedef CRITICAL_SECTION mutex_t;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
#define mutex_init(m) InitializeCriticalSection(m)
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
static double now_seconds(void) { return GetTickCount64() / 1000.0; }
static void pause_ms(int ms) { Sleep(ms); }
static void socket_timeout(socket_t s, int seconds) {
    DWORD ms = (DWORD)seconds * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
}
#else
#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
typedef int socket_t;
typedef pthread_mutex_t mutex_t;
#define BAD_SOCKET (-1)
#define close_socket close
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
static double now_seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void pause_ms(int ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}
static void socket_timeout(socket_t s, int seconds) {
    struct timeval t = {seconds, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
}
#endif
#endif
