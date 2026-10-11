# Designing a forward proxy in C

## Objective and implementation

This project implements the HTTP proxy described in the writeup. C handles socket communication, parsing, concurrent request processing, cache policy, access control and traffic logging. Winsock/native threads support Windows; POSIX sockets/pthreads support Linux. The test origin, integration suite and measurement harness are also C.

The proxy accepts an absolute HTTP target, validates request framing, applies domain and per-client policies, checks a bounded cache and forwards misses to an origin. Responses are buffered, checked and rebuilt with explicit length/connection framing. Allocated buffers have clear ownership, and independent mutexes protect shared state. A loopback listener and explicit local-destination option make the lab scope visible.

## Requirement mapping

| Writeup feature | C implementation | Evidence |
| --- | --- | --- |
| Simultaneous HTTP clients | Bounded thread admission and worker function | Concurrent real-socket requests |
| Cache hit/miss policy | TTL expiry and 16-slot LRU cache | Hit, expiry, eviction and representation-header tests |
| Access control | Domain/subdomain blocklist and sliding window limiter | 403 and 429 tests, window recovery |
| Administrator traffic logs | UTC CSV records | Parsed timestamp/client/destination/status/cache columns |
| Malformed/unreachable handling | Explicit parsing/framing/socket error responses | Bad targets, framing, truncated/oversized bodies and timeout checks |
| Performance comparison | External harness exercises compiled C process | Direct, cold and warm timing modes with hit counts |

## Performance methodology

The benchmark starts a local HTTP origin with a 50 ms response delay and launches the compiled C binary. Sixty requests are issued per mode with six concurrent clients. Direct requests bypass the proxy; cold requests use distinct query strings; warm requests repeatedly request a primed resource. The output JSON reports mean latency, nearest-rank p95, requests per second and cache hit counts.

Results must come from the C executable. Historical measurements are explicitly labeled; rerun the included C benchmark for current results. GitHub Actions retains separate Linux and Windows measurements. The controlled origin makes cache benefits visible but does not establish performance for arbitrary internet workloads.

## Limits and contribution

The implementation supports IPv4 HTTP GET/HEAD, buffers rather than streams responses, rejects HTTPS CONNECT/chunked origins and closes each connection after one request. It does not implement full RFC cache semantics or revalidation. Resource limits are explicit, and socket operation timeouts do not guarantee total DNS/request deadlines. It is intended for local evaluation.



