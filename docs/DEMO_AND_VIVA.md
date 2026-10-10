# Demonstration and viva

Build with GCC using the README command. Start a local HTTP origin, start cnproxy with --allow-private and send the same curl request twice. Identify MISS then HIT. Show a blocked domain returning 403 and inspect the CSV log.

Restart with `--allow-private --rate 2 --window 60`, send three requests and show 429. Restart with `--ttl 1`, fetch a resource, wait two seconds and show another MISS. Run the automated test suite and benchmark; identify mean/p95 latency, throughput and hit counts. Use explicit curl proxy arguments and HTTP URLs.

## Explanations each member should know

- A forward proxy is chosen by the client and retrieves resources from origin servers on the client's behalf.
- TCP is a byte stream: recv may return a partial header or body, and send may write only part of a buffer. Loops and explicit byte counts are necessary.
- GET through a proxy uses an absolute URL; the origin request uses only the path/query and a Host header.
- A mutex protects compound cache/limiter changes. Network I/O occurs outside those shared-state locks.
- Cache hits copy an owned buffer under the lock so eviction by another worker cannot free memory being sent.
- TTL controls freshness; LRU chooses an eviction victim when the bounded entry table is full.
- Rate limiting keeps request times in a sliding window per actual client IP.
- The proxy uses malloc/free for cached copies and response buffers; each error path must release its allocations and sockets.
- select waits for readiness. Nonblocking connect permits a configured connect wait rather than the OS's potentially long default.
- Winsock uses SOCKET and closesocket; POSIX uses integer descriptors and close. The platform adapter hides these differences.
- Warm cache requests avoid origin delay; cold proxy requests still perform origin I/O and add parsing/logging overhead.
- HTTPS CONNECT needs a distinct bidirectional tunnel path and is not supported here. Chunked origins require a transfer decoder, also outside this implementation.

For evaluation, explain actual limits honestly: IPv4 GET/HEAD, HTTP/1.0 outbound, no streaming/full RFC cache revalidation, bounded buffers and threads, and no guaranteed total DNS/request deadline.
