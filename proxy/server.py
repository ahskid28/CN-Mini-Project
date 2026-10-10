"""A deliberately HTTP-only teaching proxy, using bounded worker concurrency."""
import argparse
import concurrent.futures
import http.client
import ipaddress
import json
import re
import signal
import socket
import socketserver
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlsplit

from .policies import Cache, RateLimiter

DEFAULTS = dict(listen_host="127.0.0.1", listen_port=8888, workers=32,
                timeout=10, cache_ttl=30, cache_max_bytes=16 * 1024 * 1024,
                max_response_bytes=8 * 1024 * 1024, blocked_domains=[],
                rate_limit=120, rate_window=60, log_file="logs/traffic.jsonl",
                allow_private_destinations=False)
HOP = {"connection", "proxy-connection", "keep-alive", "transfer-encoding",
       "te", "trailer", "upgrade", "proxy-authorization", "proxy-authenticate"}


def clean_headers(headers):
    nominated = {token.strip().lower() for k, v in headers if k.lower() == "connection"
                 for token in v.split(",")}
    return [(k, v) for k, v in headers if k.lower() not in HOP | nominated | {"content-length", "host"}]


def cache_ttl(headers, default):
    values = {}
    for k, v in headers:
        values.setdefault(k.lower(), []).append(v)
    controls = ",".join(values.get("cache-control", [])).lower()
    directives = {part.strip().split("=", 1)[0] for part in controls.split(",")}
    if directives & {"no-store", "private", "no-cache"} or "set-cookie" in values or "vary" in values:
        return 0
    match = (re.search(r"(?:^|,)\s*s-maxage\s*=\s*\"?(\d+)", controls) or
             re.search(r"(?:^|,)\s*max-age\s*=\s*\"?(\d+)", controls))
    ttl = min(default, int(match.group(1))) if match else default
    try:
        return max(0, ttl - int(values.get("age", ["0"])[0]))
    except ValueError:
        return 0


class ProxyServer(socketserver.TCPServer):
    allow_reuse_address = True

    def __init__(self, config):
        self.config = {**DEFAULTS, **config}
        c = self.config
        for key in ("workers", "timeout", "max_response_bytes", "rate_window"):
            if not isinstance(c[key], (int, float)) or c[key] <= 0:
                raise ValueError(f"{key} must be positive")
        for key in ("cache_ttl", "cache_max_bytes", "rate_limit"):
            if not isinstance(c[key], (int, float)) or c[key] < 0:
                raise ValueError(f"{key} must be nonnegative")
        self.cache = Cache(c["cache_max_bytes"])
        self.limiter = RateLimiter(c["rate_limit"], c["rate_window"])
        self.log_lock = threading.Lock()
        self.log_path = Path(c["log_file"])
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=int(c["workers"]))
        self.slots = threading.BoundedSemaphore(int(c["workers"]))
        super().__init__((c["listen_host"], c["listen_port"]), ProxyHandler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            try:
                request.settimeout(1)
                # Consume a bounded request header before closing, avoiding
                # TCP resets caused by unread input on overloaded connections.
                pending = b""
                while b"\r\n\r\n" not in pending and len(pending) < 65536:
                    chunk = request.recv(min(4096, 65536 - len(pending)))
                    if not chunk:
                        break
                    pending += chunk
                request.sendall(b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            except OSError:
                pass
            finally:
                self.shutdown_request(request)
            return
        self.pool.submit(self.worker, request, client_address)

    def worker(self, request, address):
        try:
            request.settimeout(self.config["timeout"])
            self.finish_request(request, address)
        except (OSError, TimeoutError):
            pass
        finally:
            self.shutdown_request(request)
            self.slots.release()

    def server_close(self):
        super().server_close()
        self.pool.shutdown(wait=True)

    def record(self, event):
        with self.log_lock:
            with self.log_path.open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(event, ensure_ascii=True) + "\n")


class ProxyHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "CNProxy/1.0"

    def log_message(self, *args):
        pass

    def send_error(self, code, message=None, explain=None):
        # Parser errors can leave the default HTTP/0.9 version in place;
        # use an HTTP status line so proxy clients can interpret the error.
        if self.request_version == "HTTP/0.9":
            self.request_version = "HTTP/1.1"
        self.respond(code, [], (message or self.responses.get(code, ("Error",))[0]).encode(), "BYPASS")

    def respond(self, status, headers, body, cache_status):
        self.close_connection = True
        self.send_response(status)
        for key, value in clean_headers(headers):
            # Never trust CR/LF in response header values.
            if "\r" not in value and "\n" not in value:
                self.send_header(key, value)
        head_length = next((v for k, v in headers if k.lower() == "content-length"), "0")
        self.send_header("Content-Length", head_length if self.command == "HEAD" else str(len(body)))
        self.send_header("Connection", "close")
        self.send_header("X-Proxy-Cache", cache_status)
        self.server.record(dict(timestamp=datetime.now(timezone.utc).isoformat(),
                                client=self.client_address[0],
                                destination=getattr(self, "destination", ""),
                                method=getattr(self, "command", ""), status=status,
                                cache=cache_status, bytes=len(body),
                                latency_ms=round((time.perf_counter() - getattr(self, "started", time.perf_counter())) * 1000, 3)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_CONNECT(self):
        self.send_error(501, "HTTPS CONNECT tunneling is not implemented; use an http:// URL")

    def do_GET(self):
        self.forward()

    def do_HEAD(self):
        self.forward()

    def do_POST(self):
        self.send_error(405, "Only GET and HEAD are supported")

    do_PUT = do_POST
    do_DELETE = do_POST
    do_PATCH = do_POST
    do_OPTIONS = do_POST

    def forward(self):
        self.started = time.perf_counter()
        c = self.server.config
        try:
            url = urlsplit(self.path)
            port = url.port or 80
            host = (url.hostname or "").lower().rstrip(".")
            if (url.scheme != "http" or not host or url.username is not None or
                    url.password is not None or url.fragment or not 1 <= port <= 65535):
                raise ValueError()
            host = host.encode("idna").decode("ascii")
            self.destination = f"{host}:{port}"
            path = url.path or "/"
            if url.query:
                path += "?" + url.query
            if any(ord(ch) < 33 or ord(ch) > 126 for ch in path):
                raise ValueError()
        except (ValueError, UnicodeError):
            self.send_error(400, "Expected absolute HTTP URL, e.g. http://example.com/")
            return
        if not self.server.limiter.allow(self.client_address[0]):
            self.respond(429, [("Retry-After", str(c["rate_window"]))], b"Rate limit exceeded", "BYPASS")
            return
        if any(host == d.lower().rstrip(".") or host.endswith("." + d.lower().rstrip(".")) for d in c["blocked_domains"]):
            self.send_error(403, "Destination domain is blocked")
            return
        lengths = self.headers.get_all("Content-Length", [])
        if self.headers.get("Transfer-Encoding") or len(lengths) > 1 or (lengths and lengths[0].strip() != "0"):
            self.send_error(400, "Request bodies and transfer encoding are unsupported")
            return
        req_control = self.headers.get("Cache-Control", "").lower()
        eligible = (self.command == "GET" and not any(self.headers.get(h) for h in
                    ("Authorization", "Cookie", "Range", "If-None-Match", "If-Modified-Since",
                     "If-Match", "If-Unmodified-Since", "If-Range")) and
                    not any(d in req_control for d in ("no-cache", "no-store", "max-age=0")) and
                    self.headers.get("Pragma", "").lower() != "no-cache")
        # All forwarded end-to-end headers participate in the cache key.
        headers = clean_headers(list(self.headers.items()))
        key = (host, port, path, tuple(sorted((k.lower(), v) for k, v in headers)))
        if eligible:
            cached = self.server.cache.get(key)
            if cached:
                status, response_headers, body, inserted = cached
                response_headers = [(k, v) for k, v in response_headers if k.lower() != "age"]
                response_headers.append(("Age", str(int(time.monotonic() - inserted))))
                self.respond(status, response_headers, body, "HIT")
                return
        connection = None
        try:
            addresses = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
            if not c["allow_private_destinations"] and any(not ipaddress.ip_address(a[4][0]).is_global for a in addresses):
                self.send_error(403, "Private and non-global destinations are disabled")
                return
            # Connect to the checked address; do not perform a second DNS lookup.
            connection = http.client.HTTPConnection(addresses[0][4][0], port, timeout=c["timeout"])
            connection.putrequest(self.command, path, skip_host=True, skip_accept_encoding=True)
            authority = f"[{host}]" if ":" in host else host
            connection.putheader("Host", authority if port == 80 else f"{authority}:{port}")
            for k, v in headers:
                connection.putheader(k, v)
            connection.putheader("Connection", "close")
            connection.endheaders()
            response = connection.getresponse()
            response_headers = response.getheaders()
            body = response.read(c["max_response_bytes"] + 1)
            if self.command != "HEAD" and response.length is not None and response.length > 0:
                self.send_error(502, "Origin response is truncated or exceeds the size limit")
                return
            if len(body) > c["max_response_bytes"]:
                self.send_error(502, "Upstream response exceeds configured size limit")
                return
            ttl = cache_ttl(response_headers, c["cache_ttl"])
            if eligible and response.status == 200 and ttl > 0:
                self.server.cache.put(key, (response.status, response_headers, body, time.monotonic()), ttl)
            self.respond(response.status, response_headers, body, "MISS" if eligible else "BYPASS")
        except (socket.timeout, TimeoutError):
            self.send_error(504, "Upstream timed out")
        except (OSError, http.client.HTTPException, ValueError):
            self.send_error(502, "Unable to obtain a valid upstream response")
        finally:
            if connection:
                connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="config.json")
    args = parser.parse_args()
    with open(args.config, encoding="utf-8") as stream:
        config = json.load(stream)
    with ProxyServer(config) as server:
        stopping = threading.Event()

        def stop(*_):
            if not stopping.is_set():
                stopping.set()
                threading.Thread(target=server.shutdown, daemon=True).start()

        signal.signal(signal.SIGINT, stop)
        signal.signal(signal.SIGTERM, stop)
        print(f"HTTP proxy listening on {server.server_address[0]}:{server.server_address[1]}", flush=True)
        server.serve_forever(poll_interval=0.2)


if __name__ == "__main__":
    main()
