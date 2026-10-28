# Computer Networks Mini Project

An HTTP forward proxy implemented in C. The executable forwards real HTTP GET/HEAD requests, caches eligible responses, enforces access policies and logs traffic. The proxy, integration tests, benchmark clients and demonstration origin are entirely C. 

### Computer Networks — Mini Project Write-up

**Group Members:**

- Anveshaa Kabir Basnet (2405418)
- Ria Gracy (2405448)
- Diksha Nandi (2405493)

**Project Chosen:** Project 1 — Designing a Proxy Server

### Why We Chose This Project

We selected the proxy server project because it offers a practical, hands-on way to understand application-layer protocol handling, which connects to networking concepts covered so far in this course.

This project provides practical experience with HTTP communication, socket programming, concurrent connections, caching, access control, traffic logging and network performance measurement.

It combines multiple important networking concepts in a single system and allows us to demonstrate the working of the system through real client requests rather than only simulated inputs.

### Implemented Features

- A forward proxy that handles multiple simultaneous client connections (HTTP)
- Response caching with a defined cache-hit / cache-miss policy
- Domain blocklisting and per-client rate limiting
- Traffic logging in a format useful for a network administrator (timestamp, client, destination, cache status)
- Graceful handling of malformed requests and unreachable hosts
- A performance comparison (latency/throughput) with and without the proxy

### AI Usage Declaration

AI tools have assisted with the project in these areas:

- understanding Computer Networks concepts
- discussing and refining the system architecture
- generating initial code structures
- implementing networking and socket-programming components
- assisting with HTTP request/response parsing
- implementing caching and access-control mechanisms
- debugging errors
- generating test cases
- suggesting edge cases
- improving error handling
- assisting with C performance-testing programs
- analyzing test results
- improving code documentation
- preparing technical documentation and presentation material
- explaining unfamiliar code and concepts to team members

AI may be used during all stages of development, including planning, implementation, testing, debugging and documentation.

## Team

| Member | Roll number | Primary responsibility |
| --- | --- | --- |
| Anveshaa Kabir Basnet | 2405418 | Sockets, HTTP forwarding, thread lifecycle |
| Ria Gracy | 2405448 | Cache, domain blocking, rate limiting |
| Diksha Nandi | 2405493 | CSV logs, tests, benchmarks, documentation |

This is an AI-assisted implementation. The roles are proposed ownership for review and further work, not invented individual contributions. Each member should understand their component and commit their own actual improvements.

## Build

Linux or WSL, with GCC and Make installed:

```sh
make
./cnproxy --help
./cnproxy
```

Windows, with a working MinGW GCC:

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic src/proxy.c -o cnproxy.exe -lws2_32
.\cnproxy.exe --help
.\cnproxy.exe
```

Build the C companion programs on Windows:

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic tests/origin.c -o origin.exe -lws2_32
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic tests/test_proxy.c -o test_proxy.exe -lws2_32
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic tests/benchmark.c -o cnbenchmark.exe -lws2_32
```

Use the repository root as the working directory so the default `blocked_domains.txt` is found. The listener binds to `127.0.0.1:8888`. It uses Winsock and native Windows threads on Windows, POSIX sockets and pthreads on Linux.

## Demonstration

In terminal 1, start the included C demonstration origin:

```sh
./origin --port 8000
```

In terminal 2, allow private destinations for this local lab:

```sh
./cnproxy --allow-private
```

The demo origin serves a fixed response and special test endpoints. On Windows use `.\origin.exe --port 8000` in terminal 1 and `.\cnproxy.exe --allow-private` in terminal 2. In terminal 3:

```sh
curl --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/
curl --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/
curl --noproxy "" -i -x http://127.0.0.1:8888 http://blocked.example/
```

Use `curl.exe` in PowerShell. Expect MISS then HIT for the identical requests and 403 for the blocked domain. Inspect `traffic.csv` for UTC timestamps, client IP, destination, status, cache state, response wire bytes and processing latency.

Stop both servers with Ctrl+C. The proxy stops accepting and waits for active workers. DNS lookup duration depends on the OS resolver and can extend shutdown time.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--port` | 8888 | Loopback listening port |
| `--ttl` | 30 | Maximum cache lifetime in seconds; 0 disables storage |
| `--rate` | 120 | Requests per client IP per sliding window; 0 disables limiting |
| `--window` | 60 | Sliding-window length in seconds |
| `--timeout` | 5 | Socket operation and connect wait timeout in seconds |
| `--blocklist` | blocked_domains.txt | One domain per line; includes subdomains |
| `--log` | traffic.csv | Append-only CSV log |
| `--allow-private` | off | Permit local/private IPv4 origins for the lab |

The cache has 16 slots with LRU eviction and at most 1 MiB of response data per slot. Headers are limited to 16 KiB. At most 32 client workers are active. The limiter holds 128 client records, each with at most 1000 request timestamps. When all limiter records have live windows, new client IPs are refused rather than evicting active rate-limit state.

## Tests and benchmarks

Build and run the C harness:

```sh
make test
make benchmark
```

On Windows, after compiling all four programs:

```powershell
.\test_proxy.exe .\cnproxy.exe .\origin.exe
.\cnbenchmark.exe .\cnproxy.exe .\origin.exe benchmark-results.json
```

The harness starts and cleans up its own origin and proxy processes. Run from the repository root so `blocked_domains.txt` is available. The integration suite contains **42 checks**, including 16 concurrent requests. Tests start a deterministic local origin and launch the compiled C proxy on a free port. They cover concurrent clients, MISS/HIT, expiry, LRU eviction, blocklisting, limits, HEAD, malformed requests, oversized/truncated responses, timeout, response header filtering and CSV logs.

The benchmark compares direct, cold-proxy and warm-cache modes with 60 requests and six concurrent clients against a controlled 50 ms origin delay. Cold URLs use unique query strings; warm mode primes one resource. JSON includes mean/p95 latency, requests per second and actual hit counts. GitHub Actions builds and runs the suite and benchmark on Linux and Windows, retaining executable and measurement artifacts. The C benchmark verifies actual hit counts.

## Validation and measured results

All four C programs compiled on Windows x64 with GCC 16.2.0 using `-std=c11 -O2 -Wall -Wextra -Werror -pedantic`. The integration suite passed **42 checks with zero failures**. Linux support and CI are included; Linux was not tested locally.

The all-C benchmark measured the following on 11 October 2026, with 60 requests per mode, six concurrent clients and a controlled 50 ms origin delay:

| Mode | Mean latency | p95 latency | Requests per second | Cache hits |
| --- | ---: | ---: | ---: | ---: |
| Direct | 61.967 ms | 63.000 ms | 96.000 | 0 |
| Cold proxy | 71.883 ms | 79.000 ms | 78.329 | 0 |
| Warm cache | 2.350 ms | 16.000 ms | 1935.484 | 60 |

These local measurements illustrate caching with a delayed origin. They do not predict internet performance. Windows clock resolution can quantize short samples. Rerun `make benchmark` or the Windows command above to generate `benchmark-results.json` on your machine. The benchmark verifies zero cold hits and 60 warm hits and exits with an error if requests fail or hit counts differ.

[The older upstream measurements](docs/benchmark-windows.json) are historical results from the previous test harness. They are separate from the all-C measurements above.

## Source and documentation

- `src/proxy.c`: HTTP parser, forwarding, caching, policies, logging and main loop.
- `src/platform.h`: Windows/POSIX socket, mutex and time adapters.
- `tests/origin.c`: concurrent HTTP demo and test origin.
- `tests/lab.h`: HTTP client, socket utilities and process lifecycle.
- `tests/clients.h`: concurrent request workers.
- `tests/test_proxy.c`: C integration suite.
- `tests/benchmark.c`: direct, cold-proxy and warm-cache benchmark.
- [All-C conversion, validation and quick start](docs/ALL_C.md)
- [Architecture and limitations](docs/ARCHITECTURE.md)
- [Technical report](docs/PROJECT_REPORT.md)

## Supported scope

HTTP GET and HEAD over IPv4 are supported. HTTPS CONNECT returns 501; mutation methods return 405. Outbound requests use HTTP/1.0 and connection-close framing. Chunked origin responses are explicitly rejected with 502; streaming, persistent connections and full RFC cache revalidation are outside this mini-project. Binary bodies are supported.

Only ordinary GET 200 responses can be stored. Authentication, cookies, range, conditional and cache-control request headers bypass caching. Set-Cookie, Vary, private, no-store and no-cache responses are not stored. Origin max-age/s-maxage caps the configured TTL. Parsing is deliberately conservative and is not a full HTTP standards implementation. A default TTL is used when freshness directives are absent.

The proxy binds to loopback, has no authentication or TLS, and is intended for a local lab. The default destination filter conservatively excludes common private/reserved IPv4 ranges; it is not a comprehensive IANA registry classifier. Cached responses are held until their TTL expires without repeating DNS checks. Logs measure processing time and wire response size, not confirmed client delivery. Windows process termination by the C harness is abrupt; interactive Ctrl+C uses the shutdown handler.

