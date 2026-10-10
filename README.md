# Computer Networks Mini Project

A working HTTP forward proxy for a three-person Computer Networks project. It accepts real HTTP client requests, fetches responses from an origin server, caches eligible responses, applies access policies, and writes structured traffic logs.

## Team

| Member | Roll number | Primary area |
| --- | --- | --- |
| Anveshaa Kabir Basnet | 2405418 | HTTP forwarding, sockets, concurrency |
| Ria Gracy | 2405448 | Caching, domain filtering, rate limiting |
| Diksha Nandi | 2405493 | Logging, integration tests, benchmarks, documentation |

These are proposed responsibilities. The supplied implementation is an AI-assisted starting point; each member should study, validate and improve their assigned component and commit their actual work under their own account. The repository does not fabricate individual contribution history.

## Features

- HTTP GET and HEAD forwarding using sockets and Python's standard HTTP parser.
- Bounded thread pool; returns 503 when all workers are occupied.
- Thread-safe memory cache with TTL expiry and least-recently-used eviction.
- Domain and subdomain blocklist, and sliding-window rate limiting by client IP.
- JSON Lines logs with UTC timestamp, client, destination, method, status, cache result, body bytes and latency.
- Invalid-target/body errors, unreachable-origin errors, timeouts and response-size limits.
- Reproducible direct versus cold-proxy versus warm-cache latency/throughput benchmark.
- Unit/integration tests and GitHub Actions for Windows and Linux.

## Requirements

Python 3.11 or later. No pip packages are required. Run every command below from the repository root. On Windows, use `py` instead of `python` if necessary, and `curl.exe` instead of the PowerShell `curl` alias.

```sh
git clone https://github.com/ahskid28/CN-Mini-Project.git
cd CN-Mini-Project
python -m unittest discover -v
```

## Run the local demonstration

Terminal 1 starts an origin server with an artificial 50 ms response delay:

```sh
python -m scripts.origin --port 8000 --delay 0.05
```

Terminal 2 starts the proxy. The demo configuration allows loopback destinations:

```sh
python -m proxy --config config.demo.json
```

Terminal 3 sends two identical requests. `--noproxy ""` ensures curl does not bypass the proxy for localhost:

```sh
curl --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/
curl --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/
```

Expect `X-Proxy-Cache: MISS`, then `HIT`, and the same response body. To show blocking and a response that is never stored:

```sh
curl --noproxy "" -i -x http://127.0.0.1:8888 http://blocked.example/
curl --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/private
```

Read `logs/traffic.jsonl` to see the administrator log. Stop the proxy and origin with Ctrl+C.

For an external HTTP origin, run `python -m proxy --config config.json` and send a request to an `http://` URL. That configuration rejects loopback, private and other non-global destination IPs.

## Benchmark

```sh
python -m scripts.benchmark --requests 60 --concurrency 6 --delay 0.05 --output benchmark-results.json
```

The script starts and stops its own origin and proxy on free ports. Cold requests use distinct query strings; the warm run primes the cache first. Rate limiting is disabled only in this controlled benchmark. Inspect hit counts alongside the timing results.

| Mode | Measured mean latency | Measured p95 latency | Requests per second | Cache hits |
| --- | ---: | ---: | ---: | ---: |
| Direct | 55.764 ms | 73.807 ms | 104.668 | 0/60 |
| Cold proxy | 62.801 ms | 77.916 ms | 92.285 | 0/60 |
| Warm proxy | 9.342 ms | 15.379 ms | 537.936 | 60/60 |

These measurements were collected on Windows with Python 3.14 on 10 October 2026, using six concurrent clients and a 50 ms simulated origin delay. They are an example local result, not a claim about internet-wide performance. Rerun on the evaluation machine. Raw measurements are in [docs/benchmark-results.json](docs/benchmark-results.json).

## Configuration

| Setting | Meaning |
| --- | --- |
| `listen_host`, `listen_port` | Bind address and TCP port; default loopback:8888 |
| `workers` | Maximum simultaneous handlers, default 32 |
| `timeout` | Socket operation timeout in seconds, default 10 |
| `cache_ttl` | Maximum local cache lifetime in seconds, default 30 |
| `cache_max_bytes` | Approximate cache budget including payload/header sizes |
| `max_response_bytes` | Maximum buffered origin body size |
| `blocked_domains` | Hostnames; matching includes their subdomains |
| `rate_limit`, `rate_window` | Requests per client IP per window; limit 0 disables it |
| `log_file` | JSON Lines file path, relative to the working directory |
| `allow_private_destinations` | Allows local/private origins; enabled in demo config only |

## Project structure

```text
proxy/server.py          HTTP handlers, worker pool, forwarding, traffic logging
proxy/policies.py        TTL/LRU cache and sliding-window limiter
scripts/origin.py       Repeatable demo origin
scripts/benchmark.py    Concurrent measurements and JSON export
tests/test_proxy.py     Policy and real-socket integration checks
docs/ARCHITECTURE.md    Request flow and design decisions
docs/TEAM_PLAN.md       Three-person milestones and GitHub workflow
docs/DEMO_AND_VIVA.md   Evaluation script and explanations
docs/PROJECT_REPORT.md  Technical report and results
.github/workflows/      Automated checks on pushes and pull requests
```

## Scope and limits

This is an HTTP-only teaching implementation. It intentionally supports GET and HEAD; POST/PUT and other mutations return 405, and HTTPS CONNECT returns 501. It buffers responses rather than streaming, closes connections after one request, and does not implement full RFC cache revalidation. Cacheable GET 200 responses can use a configured heuristic TTL when an origin omits freshness directives. Authenticated/cookie/range/conditional requests bypass caching; responses with `Vary`, `Set-Cookie`, `private`, `no-store` or `no-cache` are not stored.

Keep the proxy on loopback for the lab: it has no user authentication, TLS or log rotation. DNS lookup duration depends on the OS resolver; the configured timeout covers socket operations, not a guaranteed total request deadline. The cache byte budget estimates data sizes rather than Python object overhead. Logs record origin response preparation time rather than confirmed client delivery. See the architecture notes for details.
