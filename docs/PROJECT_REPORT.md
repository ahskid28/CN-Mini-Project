# Designing an HTTP forward proxy server

## Objective

Implement and evaluate an application-layer HTTP forward proxy using real client requests. The system demonstrates socket communication, concurrent connections, caching, access control, traffic logging and performance measurement, matching the planned features in the supplied Computer Networks mini-project writeup.

## Implementation

Python's standard library provides the TCP listener, HTTP request parser, HTTP origin client, worker executor and JSON serializer. No third-party runtime packages are necessary. The proxy receives absolute HTTP request targets, validates the destination and request framing, checks rate and domain policies, looks up cacheable requests and forwards misses to the origin. It reconstructs response framing and closes the client connection after one request.

The fixed worker pool allows simultaneous clients while bounding admitted work. Cache and limiter locks protect shared state. A separate logging lock protects complete JSON Lines records. The default configuration binds only to loopback and rejects non-global origin addresses; the demo configuration explicitly permits the local test origin.

## Features and evidence

| Writeup requirement | Implementation | Verification |
| --- | --- | --- |
| Concurrent HTTP forward proxy | proxy/server.py worker pool and origin client | Sixteen requests from eight concurrent test clients |
| Cache hit and miss policy | proxy/policies.py TTL and LRU; server.py eligibility | Identical-request MISS/HIT integration check; expiry and eviction unit check |
| Access control | Domain/subdomain filter and per-IP rate limit | Blocked host/subdomain, rate window and 429 checks |
| Useful traffic logs | Structured UTC JSON Lines records | Parsed log fields and MISS/HIT assertions |
| Malformed/unreachable handling | 400/502/504 responses | Invalid targets, request body, unused origin port, timeout checks |
| Performance comparison | scripts/benchmark.py | Direct, cold and warm measurements with cache hit counts |

## Local results

The recorded sample uses Windows, Python 3.14, 60 requests per mode, concurrency six and a controlled 50 ms origin delay. Mean latency is 55.764 ms direct, 62.801 ms through the cold proxy and 9.342 ms through the warm cache. Throughput is 104.668, 92.285 and 537.936 requests per second respectively. All 60 warm requests are cache hits; all cold requests are misses. Raw results are retained in benchmark-results.json beside this report.

The cold proxy's extra work adds overhead. Warm cache requests avoid the origin's response delay and improve throughput in this workload. This result does not establish performance for arbitrary internet traffic; connection timing, origin behavior, hardware, logging cost, response sizes and cacheability all affect the comparison. Each mode uses a fresh TCP connection per request. The warm workload repeatedly requests one small resource, while the cold workload uses unique query strings to force misses.

## Limitations

The supported methods are GET and HEAD, without bodies; HTTPS CONNECT is not implemented. Responses are bounded and buffered, rather than streamed. Cache rules are intentionally conservative and do not implement full standards-based revalidation. The cache budget estimates payload/header size rather than actual process memory. Socket timeouts do not impose a total deadline on OS DNS resolution. Logging is synchronous, records processing rather than confirmed delivery and has no rotation. This project is intended for a local networking lab.

## Team and AI assistance

Anveshaa Kabir Basnet (2405418), Ria Gracy (2405448) and Diksha Nandi (2405493) are the members named in the source writeup. Proposed responsibilities and an honest incremental contribution workflow are in TEAM_PLAN.md. The implementation and documentation were prepared with AI assistance in the areas declared by the writeup. Individual contribution and understanding must be demonstrated through actual review, improvements and evaluation, rather than inferred from this generated package.

## Further work

Useful extensions are more complete cache freshness parsing, conditional revalidation, fallback across resolved addresses, streamed response bodies, total deadlines and log summaries. Each can become a real member-owned pull request with a regression test and documented behavior.
