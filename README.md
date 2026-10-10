# Computer Networks Mini Project in C

An HTTP forward proxy implemented in C, based on the group's proxy-server writeup. The executable forwards real HTTP GET/HEAD requests, caches eligible responses, enforces access policies and logs traffic. Python is used only for automated testing and benchmark clients; running the proxy does not require Python.

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

Use the repository root as the working directory so the default `blocked_domains.txt` is found. The listener binds to `127.0.0.1:8888`. It uses Winsock and native Windows threads on Windows, POSIX sockets and pthreads on Linux.

## Demonstration

In terminal 1, start any HTTP test origin. For example, Python's standard file server:

```sh
python -m http.server 8000 --bind 127.0.0.1
```

In terminal 2, allow private destinations for this local lab:

```sh
./cnproxy --allow-private
```

On Windows replace `./cnproxy` with `.\cnproxy.exe`. In terminal 3:

```sh
curl --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/README.md
curl --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/README.md
curl --noproxy "" -i -x http://127.0.0.1:8888 http://blocked.example/
```

Use `curl.exe` in PowerShell. Expect MISS then HIT for the identical requests and 403 for the blocked domain. Inspect `traffic.csv` for UTC timestamps, client IP, destination, status, cache state, response wire bytes and processing latency.

Stop with Ctrl+C. The process stops accepting and waits for active workers. DNS lookup duration depends on the OS resolver and can extend shutdown time.

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

Python 3.11+ is needed for the harness only:

```sh
python tests/test_proxy.py --binary ./cnproxy -v
python tests/benchmark.py --binary ./cnproxy --output benchmark-results.json
```

Windows: use `--binary ./cnproxy.exe`. Tests start a deterministic local origin and launch the compiled C proxy on a free port. They cover concurrent clients, MISS/HIT, expiry, LRU eviction, blocklisting, limits, HEAD, malformed requests, oversized/truncated responses, timeout, response header filtering and CSV logs.

The benchmark compares direct, cold-proxy and warm-cache modes with 60 requests and six concurrent clients against a controlled 50 ms origin delay. Cold URLs use unique query strings; warm mode primes one resource. JSON includes mean/p95 latency, requests per second and actual hit counts. GitHub Actions builds and runs the suite and benchmark on Linux and Windows, retaining executable and measurement artifacts. The previous Python implementation's timings are not reused as C measurements.

## Source and documentation

- `src/proxy.c`: HTTP parser, forwarding, caching, policies, logging and main loop.
- `src/platform.h`: Windows/POSIX socket, mutex and time adapters.
- `tests/`: Python test-only origin, clients and benchmark harness.
- [Architecture and limitations](docs/ARCHITECTURE.md)
- [Three-person contribution plan](docs/TEAM_PLAN.md)
- [Demo and viva guide](docs/DEMO_AND_VIVA.md)
- [Technical report](docs/PROJECT_REPORT.md)

## Supported scope

HTTP GET and HEAD over IPv4 are supported. HTTPS CONNECT returns 501; mutation methods return 405. Outbound requests use HTTP/1.0 and connection-close framing. Chunked origin responses are explicitly rejected with 502; streaming, persistent connections and full RFC cache revalidation are outside this mini-project. Binary bodies are supported.

Only ordinary GET 200 responses can be stored. Authentication, cookies, range, conditional and cache-control request headers bypass caching. Set-Cookie, Vary, private, no-store and no-cache responses are not stored. Origin max-age/s-maxage caps the configured TTL. Parsing is deliberately conservative and is not a full HTTP standards implementation. A default TTL is used when freshness directives are absent.

The proxy binds to loopback, has no authentication or TLS, and is intended for a local lab. The default destination filter conservatively excludes common private/reserved IPv4 ranges; it is not a comprehensive IANA registry classifier. Cached responses are held until their TTL expires without repeating DNS checks. Logs measure processing time and wire response size, not confirmed client delivery. Windows process termination by the Python harness is abrupt; interactive Ctrl+C uses the shutdown handler.
