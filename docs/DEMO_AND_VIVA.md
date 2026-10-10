# Demonstration and viva guide

## Five minute demonstration

1. Explain that the client connects to the proxy, which opens a separate connection to the origin. Show the request-flow diagram.
2. Start the origin and proxy using the README commands. Send the same URL twice; identify MISS and HIT in response headers.
3. Send `http://blocked.example/` through the proxy; show 403. Explain that subdomains are also blocked.
4. Request `/private` twice; show it is never a HIT because the origin sends no-store.
5. Stop the proxy, copy config.demo.json to config.rate-demo.json, set rate_limit to 2 and rate_window to 60, and restart with that file. Send three requests; show the third returns 429. Restart with config.demo.json afterwards.
6. Run `python -m unittest discover -v`. Show the malformed target, timeout, unreachable origin and concurrency checks.
7. Run the benchmark and explain why cold forwarding adds overhead while a cache hit avoids origin delay. Show JSON traffic records and identify timestamp, client, destination and cache status.

Use HTTP URLs. HTTPS browser pages usually require CONNECT, which this project does not implement. Curl with explicit proxy arguments makes the demonstration predictable.

## Questions everyone should answer

**What is a forward proxy?** A server selected by the client that retrieves resources from origins on the client's behalf. A reverse proxy is placed in front of origin services on the server side.

**Which layers are involved?** HTTP is at the application layer; TCP provides a reliable byte stream at the transport layer; IP routes packets at the network layer. DNS maps hostnames to addresses before the outbound connection.

**Why does TCP need HTTP framing?** TCP carries bytes and has no request/message boundaries. HTTP parsing, content lengths and chunk framing tell a receiver where messages end.

**Why use locks?** Cache ordering, cache size and request queues are mutable shared state. Multiple handlers can modify them concurrently; compound operations must remain consistent.

**Why a bounded pool?** A fixed admission limit bounds the number of handlers and outbound origin connections. Otherwise slow clients could create unbounded threads or queued work.

**What do TTL and LRU do?** TTL bounds how long cached data remains fresh locally. LRU chooses which entry to remove when the memory budget is full. They solve different problems.

**Why skip responses with cookies or Vary?** Cookies may personalize a response. Vary declares additional representation selection rules. This project uses a conservative policy and refuses to cache those responses.

**What is the rate-limit algorithm?** For each client IP, remove timestamps older than the configured window; reject if the number remaining reaches the limit, otherwise append now.

**Why block subdomains?** Exact-name blocking alone would permit sub.blocked.example. A suffix match with a dot boundary also blocks subdomains without blocking unrelated names such as notblocked.example.

**Why does cold proxy latency increase?** The proxy performs parsing, policy checks, a second connection and logging. A cold fetch still waits for the origin. A warm hit avoids origin work.

**How is throughput measured?** Completed requests divided by elapsed wall time. Mean and p95 describe per-request latencies under the configured concurrency. There are no failed requests counted as successful samples.

**Why not HTTPS?** CONNECT establishes a byte tunnel to an HTTPS endpoint. Adding it would introduce a separate bidirectional forwarding path, and the encrypted content could not be inspected/cached by this HTTP parser.

**What should be improved for production?** Authentication, complete HTTP/cache semantics, streamed bodies, total request deadlines, resilient address selection, log rotation, resource profiling and stronger configuration validation.
