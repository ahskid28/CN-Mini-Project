import argparse
import concurrent.futures
import json
import statistics
import time
from pathlib import Path
from harness import Lab

parser = argparse.ArgumentParser()
parser.add_argument("--binary", default="./cnproxy")
parser.add_argument("--requests", type=int, default=60)
parser.add_argument("--concurrency", type=int, default=6)
parser.add_argument("--output", default="benchmark-results.json")
args = parser.parse_args()
if args.requests < 1 or args.concurrency < 1:
    parser.error("requests and concurrency must be positive")
lab = Lab(args.binary, delay=0.05)
try:
    results = []
    for mode in ("direct", "proxy_cold", "proxy_warm"):
        if mode == "proxy_warm":
            lab.request()

        def sample(i):
            start = time.perf_counter()
            result = lab.request(f"/?cold={i}" if mode == "proxy_cold" else "/", direct=mode == "direct")
            if result[0] != 200 or not result[2]:
                raise RuntimeError(f"Benchmark request failed: {result[0]}")
            return (time.perf_counter()-start)*1000, result[1].get("X-Proxy-Cache", "DIRECT").strip()

        start = time.perf_counter()
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
            samples = list(pool.map(sample, range(args.requests)))
        elapsed = time.perf_counter()-start
        times = sorted(s[0] for s in samples)
        results.append(dict(mode=mode, mean_ms=round(statistics.mean(times), 3),
            p95_ms=round(times[max(0, int(len(times)*0.95+0.999)-1)], 3),
            requests_per_second=round(args.requests/elapsed, 3),
            cache_hits=sum(s[1] == "HIT" for s in samples)))
    result = dict(requests=args.requests, concurrency=args.concurrency, origin_delay_ms=50, results=results)
    Path(args.output).write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
finally:
    lab.close()
