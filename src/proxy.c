/* HTTP forward proxy. Bounds are deliberate teaching-project limits. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

#define HEADER_MAX 16384
#define RESPONSE_MAX (1024 * 1024)
#define CACHE_SLOTS 16
#define CLIENT_SLOTS 128
#define RATE_MAX 1000
#define BLOCK_MAX 64
#define WORKER_MAX 32

typedef struct { char *key, *data; size_t len; double expires, used; } CacheEntry;
typedef struct { uint32_t ip; double times[RATE_MAX]; int count; double used; } ClientRate;
typedef struct { socket_t sock; uint32_t ip; } Job;
static CacheEntry cache[CACHE_SLOTS];
static ClientRate clients[CLIENT_SLOTS];
static char blocks[BLOCK_MAX][256];
static int block_count, port = 8888, ttl = 30, rate_limit = 120, rate_window = 60;
static int allow_private, timeout_seconds = 5, active;
static mutex_t cache_mutex, rate_mutex, log_mutex, active_mutex;
static FILE *log_file;
static volatile sig_atomic_t stopping;

static void stop_signal(int value) { (void)value; stopping = 1; }
static char *copy_text(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
static int send_all(socket_t s, const char *data, size_t n) {
    while (n) {
        int sent = send(s, data, (int)n, 0);
        if (sent <= 0) return 0;
        data += sent; n -= (size_t)sent;
    }
    return 1;
}
static void log_event(uint32_t ip, const char *host, int status, const char *state,
                      size_t bytes, double start) {
    time_t t = time(NULL);
    struct tm tm_value;
    char stamp[40];
    mutex_lock(&log_mutex);
    tm_value = *gmtime(&t);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm_value);
    fprintf(log_file, "%s,%u.%u.%u.%u,%s,%d,%s,%lu,%.3f\n", stamp,
            (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255,
            host, status, state, (unsigned long)bytes, (now_seconds() - start) * 1000);
    fflush(log_file);
    mutex_unlock(&log_mutex);
}
static void error_response(socket_t s, int code, const char *reason, int head) {
    char buffer[1024];
    int n = snprintf(buffer, sizeof(buffer),
        "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\n"
        "Connection: close\r\nX-Proxy-Cache: BYPASS\r\n\r\n%s", code, reason,
        (unsigned long)strlen(reason), reason);
    if (head) {
        char *end = strstr(buffer, "\r\n\r\n");
        if (end) n = (int)(end-buffer)+4;
    }
    send_all(s, buffer, (size_t)n);
}

static int blocked(const char *host) {
    int i;
    size_t n = strlen(host);

    /* Treat a trailing dot as equivalent to no trailing dot. */
    if (n > 0 && host[n - 1] == '.') {
        n--;
    }

    for (i = 0; i < block_count; i++) {
        size_t k = strlen(blocks[i]);

        if (n == k && !strncasecmp(host, blocks[i], n)) {
            return 1;
        }

        if (n > k && host[n - k - 1] == '.' &&
            !strncasecmp(host + n - k, blocks[i], k)) {
            return 1;
        }
    }

    return 0;
}
static int rate_allowed(uint32_t ip) {
    int i, j, slot = -1, oldest = 0;
    double now = now_seconds();
    ClientRate *r;
    if (!rate_limit) return 1;
    mutex_lock(&rate_mutex);
    for (i = 0; i < CLIENT_SLOTS; i++) {
        if (clients[i].count && clients[i].ip == ip) { slot = i; break; }
        if (clients[i].used < clients[oldest].used) oldest = i;
    }
    if (slot < 0) {
        slot = oldest;
        /* Refuse a new client if all slots still contain live windows. */
        if (clients[slot].count && now - clients[slot].used < rate_window) {
            mutex_unlock(&rate_mutex); return 0;
        }
        clients[slot].count = 0; clients[slot].ip = ip;
    }
    r = &clients[slot];
    for (i = 0, j = 0; i < r->count; i++)
        if (r->times[i] > now - rate_window) r->times[j++] = r->times[i];
    r->count = j; r->used = now;
    if (j >= rate_limit) { mutex_unlock(&rate_mutex); return 0; }
    r->times[r->count++] = now;
    mutex_unlock(&rate_mutex); return 1;
}

static char *cache_get(const char *key, size_t *length) {
    int i;
    char *result = NULL;
    double now = now_seconds();

    mutex_lock(&cache_mutex);

    for (i = 0; i < CACHE_SLOTS; i++) {
        if (!cache[i].key) {
            continue;
        }

        if (cache[i].expires <= now) {
            free(cache[i].key);
            free(cache[i].data);
            cache[i].key = NULL;
            cache[i].data = NULL;
            cache[i].len = 0;
            cache[i].expires = 0;
            cache[i].used = 0;
            continue;
        }

        if (!strcmp(cache[i].key, key)) {
            result = malloc(cache[i].len);

            if (result) {
                memcpy(result, cache[i].data, cache[i].len);
                *length = cache[i].len;
                cache[i].used = now;
            }

            break;
        }
    }

    mutex_unlock(&cache_mutex);
    return result;
}
static void cache_put(const char *key, const char *data, size_t n, int lifetime) {
    int i, slot = 0;
    char *new_key, *new_data;
    double now = now_seconds();
    if (lifetime <= 0) return;
    new_key = copy_text(key); new_data = malloc(n);
    if (!new_key || !new_data) { free(new_key); free(new_data); return; }
    memcpy(new_data, data, n);
    mutex_lock(&cache_mutex);
    for (i = 0; i < CACHE_SLOTS; i++) {
        if (!cache[i].key || cache[i].expires <= now || !strcmp(cache[i].key, key)) { slot = i; break; }
        if (cache[i].used < cache[slot].used) slot = i;
    }
    free(cache[slot].key); free(cache[slot].data);
    cache[slot].key = new_key; cache[slot].data = new_data; cache[slot].len = n;
    cache[slot].used = now; cache[slot].expires = now + lifetime;
    mutex_unlock(&cache_mutex);
}
/* Header values are copied into bounded buffers, without modifying wire data. */
static int header_value(const char *headers, const char *name, char *out, size_t capacity) {
    const char *p = strstr(headers, "\r\n");
    size_t name_len = strlen(name);
    int count = 0;
    if (!p) return 0;
    p += 2;
    while (*p && strncmp(p, "\r\n", 2)) {
        const char *end = strstr(p, "\r\n"), *colon;
        if (!end) return -1;
        colon = memchr(p, ':', (size_t)(end-p));
        if (!colon || colon == p || p[0] == ' ' || p[0] == '\t') return -1;
        if ((size_t)(colon-p) == name_len && !strncasecmp(p, name, name_len)) {
            const char *v = colon+1;
            size_t n;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            n = (size_t)(end-v);
            if (n >= capacity) return -1;
            memcpy(out, v, n); out[n] = 0; count++;
        }
        p = end+2;
    }
    return count;
}
static int hop_header(const char *name, const char *connection) {
    const char *p = connection;
    if (!strcasecmp(name, "Connection") || !strcasecmp(name, "Proxy-Connection") ||
        !strcasecmp(name, "Keep-Alive") || !strcasecmp(name, "Transfer-Encoding") ||
        !strcasecmp(name, "TE") || !strcasecmp(name, "Trailer") ||
        !strcasecmp(name, "Upgrade") || !strcasecmp(name, "Proxy-Authorization") ||
        !strcasecmp(name, "Proxy-Authenticate") || !strcasecmp(name, "X-Proxy-Cache")) return 1;
    while (*p) {
        const char *end;
        size_t n;
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        end = p;
        while (*end && *end != ',') end++;
        n = (size_t)(end-p);
        while (n && (p[n-1] == ' ' || p[n-1] == '\t')) n--;
        if (strlen(name) == n && !strncasecmp(name, p, n)) return 1;
        p = end;
    }
    return 0;
}
static int append_headers(char *out, size_t cap, size_t *used, const char *input) {
    const char *p = strstr(input, "\r\n");
    char connection[4096] = "";
    if (!p) return 0;
    { int count = header_value(input, "Connection", connection, sizeof(connection));
      if (count < 0 || count > 1) return 0; }
    p += 2;
    while (*p && strncmp(p, "\r\n", 2)) {
        const char *end = strstr(p, "\r\n"), *colon;
        char name[256];
        size_t n, k;
        if (!end) return 0;
        colon = memchr(p, ':', (size_t)(end-p));
        if (!colon || colon == p || (size_t)(colon-p) >= sizeof(name)) return 0;
        n = (size_t)(colon-p); memcpy(name, p, n); name[n] = 0;
        for (k = 0; k < n; k++)
            if (!isalnum((unsigned char)name[k]) && !strchr("!#$%&'*+-.^_`|~", name[k])) return 0;
        for (k = n+1; p+k < end; k++)
            if (((unsigned char)p[k] < 32 && p[k] != '\t') || (unsigned char)p[k] == 127) return 0;
        if (!hop_header(name, connection) && strcasecmp(name, "Host") && strcasecmp(name, "Content-Length")) {
            n = (size_t)(end-p) + 2;
            if (*used + n >= cap) return 0;
            memcpy(out+*used, p, n); *used += n; out[*used] = 0;
        }
        p = end+2;
    }
    return 1;
}
static int parse_target(const char *target, char *host, int *origin_port, char *path) {
    const char *p, *end, *colon;
    size_t n, i;
    if (strncmp(target, "http://", 7)) return 0;
    p = target+7; end = p + strcspn(p, "/?#"); n = (size_t)(end-p);
    if (!n || n >= 256 || memchr(p, '@', n) || memchr(p, '[', n) || strchr(target, '#')) return 0;
    colon = memchr(p, ':', n); *origin_port = 80;
    if (colon) {
        char *tail;
        long value = strtol(colon+1, &tail, 10);
        if (tail != end || value < 1 || value > 65535) return 0;
        n = (size_t)(colon-p); *origin_port = (int)value;
    }
    if (!n) return 0;
    for (i = 0; i < n; i++) {
        if (!isalnum((unsigned char)p[i]) && p[i] != '-' && p[i] != '.') return 0;
        host[i] = (char)tolower((unsigned char)p[i]);
    }
    if (host[n-1] == '.') n--;
    if (!n) return 0;
    host[n] = 0;
    if (strlen(end) > 4094) return 0;
    if (*end == '?') snprintf(path, 4096, "/%s", end);
    else strcpy(path, *end ? end : "/");
    for (i = 0; path[i]; i++) if ((unsigned char)path[i] < 33 || (unsigned char)path[i] > 126) return 0;
    return 1;
}
static int global_ipv4(uint32_t ip) {
    unsigned a = ip >> 24, b = (ip >> 16) & 255;
    return !(a == 0 || a == 10 || a == 127 || a >= 224 ||
             (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) ||
             (a == 192 && (b == 168 || b == 0)) || (a == 100 && b >= 64 && b <= 127) ||
             (a == 198 && (b == 18 || b == 19 || b == 51)) || (a == 203 && b == 0));
}
static socket_t connect_origin(const char *host, int destination_port, int *error) {
    struct addrinfo hints, *result, *p;
    char service[8];
    socket_t s = BAD_SOCKET;
    memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", destination_port);
    if (getaddrinfo(host, service, &hints, &result)) { *error = 502; return BAD_SOCKET; }
    if (!allow_private) {
        for (p = result; p; p = p->ai_next)
            if (!global_ipv4(ntohl(((struct sockaddr_in *)p->ai_addr)->sin_addr.s_addr))) {
                freeaddrinfo(result); *error = 403; return BAD_SOCKET;
            }
    }
    for (p = result; p; p = p->ai_next) {
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == BAD_SOCKET) continue;
        socket_timeout(s, timeout_seconds);
        /* Nonblocking connect with a bounded wait, including on Windows. */
#ifdef _WIN32
        { u_long mode = 1; ioctlsocket(s, FIONBIO, &mode); }
#else
        fcntl(s, F_SETFL, O_NONBLOCK);
#endif
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        {
            fd_set writes; struct timeval wait = {timeout_seconds, 0};
            int e = 1;
#ifdef _WIN32
            int len = sizeof(e);
#else
            socklen_t len = sizeof(e);
#endif
            FD_ZERO(&writes); FD_SET(s, &writes);
            if (select((int)s+1, NULL, &writes, NULL, &wait) > 0 &&
                !getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&e, &len) && !e) break;
        }
        close_socket(s); s = BAD_SOCKET;
    }
    freeaddrinfo(result);
#ifdef _WIN32
    if (s != BAD_SOCKET) { u_long mode = 0; ioctlsocket(s, FIONBIO, &mode); }
#else
    if (s != BAD_SOCKET) fcntl(s, F_SETFL, 0);
#endif
    if (s == BAD_SOCKET) *error = 502;
    return s;
}
static int lifetime(const char *headers) {
    char control[4096] = "", temp[4096], age[64] = "0";
    int life = ttl, i, count, max_age = -1, shared_age = -1;
    char *p, *tail;
    long age_value;
    count = header_value(headers, "Cache-Control", control, sizeof(control));
    if (count < 0 || count > 1) return 0;
    for (i = 0; control[i]; i++) control[i] = (char)tolower((unsigned char)control[i]);
    if (strstr(control, "no-store") || strstr(control, "no-cache") || strstr(control, "private") ||
        header_value(headers, "Set-Cookie", temp, sizeof(temp)) ||
        header_value(headers, "Vary", temp, sizeof(temp))) return 0;
    p = control;
    while (*p) {
        char *end = strchr(p, ','), *eq, *name_end;
        long value;
        if (end) *end = 0;
        while (*p == ' ' || *p == '\t') p++;
        eq = strchr(p, '=');
        if (eq) {
            name_end = eq;
            while (name_end > p && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
            *name_end = 0;
            if (!strcmp(p, "max-age") || !strcmp(p, "s-maxage")) {
                char *v = eq+1;
                int quoted;
                while (*v == ' ' || *v == '\t') v++;
                quoted = *v == '"'; if (quoted) v++;
                if (!isdigit((unsigned char)*v)) return 0;
                value = strtol(v, &tail, 10);
                if (quoted) { if (*tail != '"') return 0; tail++; }
                while (*tail == ' ' || *tail == '\t') tail++;
                if (*tail || value < 0) return 0;
                if (value > ttl) value = ttl;
                if (!strcmp(p, "s-maxage")) {
                    if (shared_age >= 0) return 0;
                    shared_age = (int)value;
                } else {
                    if (max_age >= 0) return 0;
                    max_age = (int)value;
                }
            }
        }
        if (!end) break;
        p = end+1;
    }
    if (shared_age >= 0) life = shared_age;
    else if (max_age >= 0) life = max_age;
    count = header_value(headers, "Age", age, sizeof(age));
    if (count < 0 || count > 1) return 0;
    age_value = strtol(age, &tail, 10);
    if (!*age || *tail || age_value < 0 || age_value >= life) return 0;
    return life - (int)age_value;
}
static void process_client(Job *job) {
    char request[HEADER_MAX+1], method[16], target[4096], version[16], extra[2];
    char host[256] = "-", path[4096], value[4096], outbound[HEADER_MAX+4096];
    char *raw = NULL, *reply = NULL, *cached = NULL, *end;
    size_t used = 0, n = 0, header_len, body_len, out_len;
    int origin_port, status = 400, eligible, head = 0, r, length_count;
    long declared = -1;
    socket_t upstream = BAD_SOCKET;
    const char *state = "BYPASS", *reason = "Bad Request";
    double start = now_seconds();
    socket_timeout(job->sock, timeout_seconds);
    while (used < HEADER_MAX) {
        r = recv(job->sock, request+used, (int)(HEADER_MAX-used), 0);
        if (r <= 0) goto failure;
        used += (size_t)r; request[used] = 0;
        if (memchr(request, 0, used)) goto failure;
        if (strstr(request, "\r\n\r\n")) break;
    }
    end = strstr(request, "\r\n\r\n");
    if (!end || (size_t)(end-request)+4 != used) goto failure;
    end = strstr(request, "\r\n");
    if (!end) goto failure;
    *end = 0;
    r = sscanf(request, "%15s %4095s %15s %1s", method, target, version, extra);
    *end = '\r';
    if (r != 3 || (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) goto failure;
    if (!strcmp(method, "CONNECT")) { status = 501; reason = "HTTPS CONNECT Unsupported"; goto failure; }
    head = !strcmp(method, "HEAD");
    if (strcmp(method, "GET") && !head) { status = 405; reason = "Method Not Allowed"; goto failure; }
    if (!parse_target(target, host, &origin_port, path)) goto failure;
    if (!rate_allowed(job->ip)) { status = 429; reason = "Rate Limit Exceeded"; goto failure; }
    if (blocked(host)) { status = 403; reason = "Domain Blocked"; goto failure; }
    length_count = header_value(request, "Content-Length", value, sizeof(value));
    if (length_count < 0 || length_count > 1 || (length_count && strcmp(value, "0")) ||
        header_value(request, "Transfer-Encoding", value, sizeof(value))) goto failure;
    eligible = !head && !header_value(request, "Authorization", value, sizeof(value)) &&
        !header_value(request, "Cookie", value, sizeof(value)) &&
        !header_value(request, "Range", value, sizeof(value)) &&
        !header_value(request, "If-None-Match", value, sizeof(value)) &&
        !header_value(request, "If-Modified-Since", value, sizeof(value)) &&
        !header_value(request, "If-Match", value, sizeof(value)) &&
        !header_value(request, "If-Unmodified-Since", value, sizeof(value)) &&
        !header_value(request, "If-Range", value, sizeof(value)) &&
        !header_value(request, "Cache-Control", value, sizeof(value)) &&
        !header_value(request, "Pragma", value, sizeof(value));
    out_len = (size_t)snprintf(outbound, sizeof(outbound), "%s %s HTTP/1.0\r\nHost: %s:%d\r\n", method, path, host, origin_port);
    if (!append_headers(outbound, sizeof(outbound)-32, &out_len, request)) goto failure;
    memcpy(outbound+out_len, "Connection: close\r\n\r\n", 22); out_len += 21;
    if (eligible) cached = cache_get(outbound, &n);
    if (cached) { reply = cached; cached = NULL; state = "HIT"; status = 200; goto success; }
    upstream = connect_origin(host, origin_port, &status);
    if (upstream == BAD_SOCKET) { reason = status == 403 ? "Private Destination Disabled" : "Origin Unreachable"; goto failure; }
    if (!send_all(upstream, outbound, out_len)) { status = 502; reason = "Origin Send Failed"; goto failure; }
    raw = malloc(RESPONSE_MAX+1); reply = malloc(RESPONSE_MAX+HEADER_MAX);
    if (!raw || !reply) { status = 503; reason = "Allocation Failed"; goto failure; }
    used = 0;
    while (used < RESPONSE_MAX) {
        r = recv(upstream, raw+used, (int)(RESPONSE_MAX-used), 0);
        if (r < 0) { status = 504; reason = "Origin Timeout Or Read Failure"; goto failure; }
        if (!r) break;
        used += (size_t)r;
    }
    raw[used] = 0;
    if (used == RESPONSE_MAX) { status = 502; reason = "Response Too Large"; goto failure; }
    end = strstr(raw, "\r\n\r\n");
    if (!end || (size_t)(end-raw) > HEADER_MAX || sscanf(raw, "HTTP/%15s %d", version, &status) != 2 || status < 200 || status > 599) {
        status = 502; reason = "Invalid Origin Response"; goto failure;
    }
    header_len = (size_t)(end-raw)+4; body_len = used-header_len;
    if (header_value(raw, "Transfer-Encoding", value, sizeof(value))) {
        status = 502; reason = "Chunked Origin Response Unsupported"; goto failure;
    }
    length_count = header_value(raw, "Content-Length", value, sizeof(value));
    if (length_count < 0 || length_count > 1) { status = 502; reason = "Invalid Origin Framing"; goto failure; }
    if (length_count) {
        char *tail;
        declared = strtol(value, &tail, 10);
        if (!*value || *tail || declared < 0 || (!head && (size_t)declared != body_len)) {
            status = 502; reason = "Truncated Or Invalid Origin Body"; goto failure;
        }
    }
    n = (size_t)snprintf(reply, RESPONSE_MAX+HEADER_MAX, "HTTP/1.1 %d Origin Response\r\n", status);
    if (!append_headers(reply, RESPONSE_MAX+HEADER_MAX-256, &n, raw)) {
        status = 502; reason = "Invalid Origin Headers"; goto failure;
    }
    state = eligible ? "MISS" : "BYPASS";
    n += (size_t)snprintf(reply+n, 256, "Content-Length: %lu\r\nConnection: close\r\nX-Proxy-Cache: %s\r\n\r\n",
        (unsigned long)(head && declared >= 0 ? (size_t)declared : body_len), state);
    if (!head) { memcpy(reply+n, raw+header_len, body_len); n += body_len; }
    if (eligible && status == 200) {
        char *flag = strstr(reply, "X-Proxy-Cache: MISS");
        if (flag) {
            /* HIT is padded to preserve the cached message byte count. */
            memcpy(flag+15, "HIT ", 4);
            cache_put(outbound, reply, n, lifetime(raw));
            memcpy(flag+15, "MISS", 4);
        }
    }
success:
    log_event(job->ip, host, status, state, n, start);
    send_all(job->sock, reply, n);
    goto cleanup;
failure:
    log_event(job->ip, host, status, "BYPASS", 0, start);
    error_response(job->sock, status, reason, head);
cleanup:
    if (upstream != BAD_SOCKET) close_socket(upstream);
    free(raw); free(reply); free(cached);
}
#ifdef _WIN32
static DWORD WINAPI worker(LPVOID argument)
#else
static void *worker(void *argument)
#endif
{
    Job *job = argument;
    process_client(job); close_socket(job->sock); free(job);
    mutex_lock(&active_mutex); active--; mutex_unlock(&active_mutex);
    return 0;
}
static int integer_arg(const char *s, int low, int high) {
    char *end; long value = strtol(s, &end, 10);
    if (!*s || *end || value < low || value > high) return -1;
    return (int)value;
}
int main(int argc, char **argv) {
    socket_t listener;
    struct sockaddr_in address;
    const char *log_path = "traffic.csv", *block_path = "blocked_domains.txt";
    int i, option = 1;
#ifdef _WIN32
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2,2), &ws)) return 1;
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--allow-private")) allow_private = 1;
        else if (!strcmp(argv[i], "--help")) {
            puts("cnproxy [--port 8888] [--ttl 30] [--rate 120] [--window 60] [--timeout 5]\n"
                 "        [--blocklist blocked_domains.txt] [--log traffic.csv] [--allow-private]"); return 0;
        } else if (i+1 < argc) {
            const char *name = argv[i], *value = argv[++i];
            if (!strcmp(name, "--port")) port = integer_arg(value, 1, 65535);
            else if (!strcmp(name, "--ttl")) ttl = integer_arg(value, 0, 86400);
            else if (!strcmp(name, "--rate")) rate_limit = integer_arg(value, 0, RATE_MAX);
            else if (!strcmp(name, "--window")) rate_window = integer_arg(value, 1, 86400);
            else if (!strcmp(name, "--timeout")) timeout_seconds = integer_arg(value, 1, 60);
            else if (!strcmp(name, "--log")) log_path = value;
            else if (!strcmp(name, "--blocklist")) block_path = value;
            else { fprintf(stderr, "Unknown option: %s\n", name); return 1; }
        } else { fprintf(stderr, "Missing option value\n"); return 1; }
    }
    if (port < 0 || ttl < 0 || rate_limit < 0 || rate_window < 0 || timeout_seconds < 0) {
        fprintf(stderr, "Invalid numeric option\n"); return 1;
    }
    {
        FILE *file = fopen(block_path, "r"); char line[512];
        if (!file) { perror(block_path); return 1; }
        while (fgets(line, sizeof(line), file)) {
            size_t k, length = strcspn(line, "\r\n"); line[length] = 0;
            if (!length || line[0] == '#') continue;
            if (length >= 256 || block_count == BLOCK_MAX) { fclose(file); fprintf(stderr, "Blocklist limit exceeded\n"); return 1; }
            for (k = 0; k < length; k++) {
                if (!isalnum((unsigned char)line[k]) && line[k] != '.' && line[k] != '-') { fclose(file); fprintf(stderr, "Invalid blocklist domain\n"); return 1; }
                line[k] = (char)tolower((unsigned char)line[k]);
            }
            if (line[length-1] == '.') line[--length] = 0;
            if (!length) { fclose(file); fprintf(stderr, "Empty blocklist domain\n"); return 1; }
            strcpy(blocks[block_count++], line);
        }
        fclose(file);
    }
    log_file = fopen(log_path, "a");
    if (!log_file) { perror(log_path); return 1; }
    if (ftell(log_file) == 0) fputs("timestamp_utc,client,destination,status,cache,wire_bytes,latency_ms\n", log_file);
    mutex_init(&cache_mutex); mutex_init(&rate_mutex); mutex_init(&log_mutex); mutex_init(&active_mutex);
    signal(SIGINT, stop_signal); signal(SIGTERM, stop_signal);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&option, sizeof(option));
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener == BAD_SOCKET || bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 64)) {
        fprintf(stderr, "Cannot listen on 127.0.0.1:%d\n", port); fclose(log_file); return 1;
    }
    printf("C HTTP proxy listening on 127.0.0.1:%d\n", port); fflush(stdout);
    while (!stopping) {
        fd_set reads; struct timeval wait = {0, 200000};
        socket_t client; Job *job;
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        FD_ZERO(&reads); FD_SET(listener, &reads);
        if (select((int)listener+1, &reads, NULL, NULL, &wait) <= 0) continue;
        client = accept(listener, (struct sockaddr *)&address, &length);
        if (client == BAD_SOCKET) continue;
        mutex_lock(&active_mutex);
        if (active >= WORKER_MAX) {
            mutex_unlock(&active_mutex); socket_timeout(client, 1);
            {
                char discard[HEADER_MAX+1]; size_t used = 0;
                while (used < HEADER_MAX) {
                    int got = recv(client, discard+used, (int)(HEADER_MAX-used), 0);
                    if (got <= 0) break;
                    used += (size_t)got; discard[used] = 0;
                    if (strstr(discard, "\r\n\r\n")) break;
                }
            }
            error_response(client, 503, "Worker Limit Reached", 0); close_socket(client); continue;
        }
        active++; mutex_unlock(&active_mutex);
        job = malloc(sizeof(*job));
        if (!job) { close_socket(client); mutex_lock(&active_mutex); active--; mutex_unlock(&active_mutex); continue; }
        job->sock = client; job->ip = ntohl(address.sin_addr.s_addr);
#ifdef _WIN32
        { HANDLE thread = CreateThread(NULL, 0, worker, job, 0, NULL);
          if (thread) { CloseHandle(thread); continue; } }
#else
        { pthread_t thread;
          if (!pthread_create(&thread, NULL, worker, job)) { pthread_detach(thread); continue; } }
#endif
        close_socket(client); free(job); mutex_lock(&active_mutex); active--; mutex_unlock(&active_mutex);
    }
    close_socket(listener);
    for (;;) {
        int remaining;
        mutex_lock(&active_mutex); remaining = active; mutex_unlock(&active_mutex);
        if (!remaining) break;
        pause_ms(50);
    }
    for (i = 0; i < CACHE_SLOTS; i++) { free(cache[i].key); free(cache[i].data); }
    fclose(log_file);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
