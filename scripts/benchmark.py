"""Compare direct, cold proxy and warm proxy using a controlled local origin."""
import argparse
import concurrent.futures
import http.client
import json
import statistics
import tempfile
import threading
import time
from pathlib import Path

from proxy.server import ProxyServer
from scripts.origin import make_origin


def run(count=60, concurrency=6, delay=0.05):
    with tempfile.TemporaryDirectory() as temp:
        origin = make_origin(delay)
        proxy = ProxyServer(dict(listen_port=0, rate_limit=0, allow_private_destinations=True,
                                 cache_ttl=300, log_file=str(Path(temp) / "traffic.jsonl")))
        threads = []
        for server in (origin, proxy):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            threads.append(thread)
        url = f"http://127.0.0.1:{origin.server_port}/"

        def request(mode, i):
            start = time.perf_counter()
            address = origin.server_address if mode == "direct" else proxy.server_address
            target = "/" if mode == "direct" else url + (f"?cold={i}" if mode == "proxy_cold" else "")
            connection = http.client.HTTPConnection(*address, timeout=10)
            try:
                connection.request("GET", target)
                response = connection.getresponse()
                body = response.read()
                if response.status != 200 or not body:
                    raise RuntimeError(f"Unexpected benchmark response: {response.status}")
                return (time.perf_counter() - start) * 1000, response.getheader("X-Proxy-Cache", "DIRECT")
            finally:
                connection.close()

        try:
            results = []
            for mode in ("direct", "proxy_cold", "proxy_warm"):
                if mode == "proxy_warm":
                    request(mode, -1)
                start = time.perf_counter()
                with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as pool:
                    samples = list(pool.map(lambda i: request(mode, i), range(count)))
                elapsed = time.perf_counter() - start
                timings = sorted(item[0] for item in samples)
                results.append(dict(mode=mode, requests=count, concurrency=concurrency,
                                    mean_ms=round(statistics.mean(timings), 3),
                                    p95_ms=round(timings[max(0, int(0.95 * len(timings) + 0.999) - 1)], 3),
                                    requests_per_second=round(count / elapsed, 3),
                                    cache_hits=sum(s[1] == "HIT" for s in samples)))
            return dict(origin_delay_seconds=delay, results=results)
        finally:
            for server in (proxy, origin):
                server.shutdown()
                server.server_close()
            for thread in threads:
                thread.join()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--requests", type=int, default=60)
    parser.add_argument("--concurrency", type=int, default=6)
    parser.add_argument("--delay", type=float, default=0.05)
    parser.add_argument("--output", default="benchmark-results.json")
    args = parser.parse_args()
    if args.requests < 1 or args.concurrency < 1 or args.delay < 0:
        parser.error("requests and concurrency must be positive; delay must be nonnegative")
    result = run(args.requests, args.concurrency, args.delay)
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
