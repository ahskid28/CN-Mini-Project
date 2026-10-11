# Entirely C version

Based on ahskid28/CN-Mini-Project main retrieved on 11 October 2026.
The existing C proxy is preserved. Python origin, test suite and benchmark
have been replaced with C programs. Build and CI need no Python.

## Files

- src/proxy.c and src/platform.h: original proxy and operating-system adapters.
- tests/origin.c: concurrent, deterministic HTTP origin; also used for demos.
- tests/lab.h: sockets, HTTP client, subprocess lifecycle and local lab setup.
- tests/clients.h: concurrent benchmark and integration clients.
- tests/test_proxy.c: 42 assertions, including 16 simultaneous client requests.
- tests/benchmark.c: 60 requests per mode with six workers; emits JSON.

## Validation

Windows x64, GCC 16.2.0 from w64devkit 2.10.0:
all four programs compiled with C11, -O2, -Wall, -Wextra, -Werror and -pedantic.
42 checks passed with zero failures. The C benchmark completed successfully,
with zero cold hits and all 60 warm requests hitting the cache.
Current measurements are in ../benchmark-results.json.
Linux code paths and the Linux CI workflow are included but were not run locally.

The benchmark uses the platform's monotonic clock. Windows tick resolution can
quantize short samples; these local results are illustrative measurements.
The harness chooses ephemeral ports before spawning children, so another
process can occasionally claim a selected port; rerun if startup fails.
The POSIX subprocess adapter takes simple space-separated arguments; pass
binary paths as individual command-line arguments (paths may contain spaces),
and keep lab-generated option values free of spaces.
Windows harness cleanup terminates child processes. Interactive proxy Ctrl+C
uses the proxy's graceful shutdown handler.

## Windows quick start

Run from the project directory:

```powershell
.\origin.exe --port 8000
```

In another terminal:

```powershell
.\cnproxy.exe --allow-private
```

In a third terminal:

```powershell
curl.exe --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/
curl.exe --noproxy "" -i -x http://127.0.0.1:8888 http://127.0.0.1:8000/
```

Expect MISS then HIT. Stop both servers before running the standalone harness:

```powershell
.\test_proxy.exe .\cnproxy.exe .\origin.exe
.\cnbenchmark.exe .\cnproxy.exe .\origin.exe benchmark-results.json
```

On Linux run make, make test and make benchmark.
