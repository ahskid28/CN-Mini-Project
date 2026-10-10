import concurrent.futures
import http.client
import json
import socket
import tempfile
import threading
import unittest
from pathlib import Path

from proxy.policies import Cache, RateLimiter
from proxy.server import ProxyServer, cache_ttl, clean_headers
from scripts.origin import make_origin


class Policies(unittest.TestCase):
    def test_expiry_and_eviction(self):
        now = [0]
        cache = Cache(300, lambda: now[0])
        value = (200, [], b"x" * 100, 0)
        cache.put("a", value, 2)
        cache.put("b", value, 2)
        self.assertIsNone(cache.get("a"))
        self.assertEqual(cache.get("b"), value)
        now[0] = 2
        self.assertIsNone(cache.get("b"))

    def test_rate_window(self):
        now = [0]
        limiter = RateLimiter(2, 10, lambda: now[0])
        self.assertTrue(limiter.allow("a"))
        self.assertTrue(limiter.allow("a"))
        self.assertFalse(limiter.allow("a"))
        self.assertTrue(limiter.allow("b"))
        now[0] = 10
        self.assertTrue(limiter.allow("a"))

    def test_cache_directives(self):
        for header in ("private", "no-cache", "no-store", "max-age=0"):
            self.assertEqual(cache_ttl([("Cache-Control", header)], 30), 0)
        self.assertEqual(cache_ttl([("Vary", "Accept")], 30), 0)
        self.assertEqual(cache_ttl([("Set-Cookie", "a=b")], 30), 0)
        self.assertEqual(cache_ttl([("Cache-Control", "max-age=10"), ("Age", "3")], 30), 7)
        self.assertEqual(cache_ttl([("Cache-Control", "max-age=20, s-maxage=5")], 30), 5)

    def test_hop_headers(self):
        self.assertEqual(clean_headers([("Connection", "X-Secret"), ("X-Secret", "a"),
                                       ("Proxy-Authorization", "hidden"), ("Accept", "text/plain")]),
                         [("Accept", "text/plain")])


class Integration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.origin = make_origin(0.02)
        self.proxy = ProxyServer(dict(listen_port=0, allow_private_destinations=True,
                                      rate_limit=0, blocked_domains=["blocked.example"],
                                      timeout=1, max_response_bytes=1024,
                                      log_file=str(Path(self.temp.name) / "traffic.jsonl")))
        self.threads = []
        for server in (self.origin, self.proxy):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            self.threads.append(thread)
        self.url = f"http://127.0.0.1:{self.origin.server_port}"

    def tearDown(self):
        for server in (self.proxy, self.origin):
            server.shutdown()
            server.server_close()
        for thread in self.threads:
            thread.join()
        self.temp.cleanup()

    def request(self, target=None, method="GET", headers=None):
        connection = http.client.HTTPConnection(*self.proxy.server_address, timeout=3)
        try:
            connection.request(method, target or self.url + "/", headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_cache_and_log(self):
        first = self.request()
        second = self.request()
        self.assertEqual(first[0], 200)
        self.assertEqual(first[1]["X-Proxy-Cache"], "MISS")
        self.assertEqual(second[1]["X-Proxy-Cache"], "HIT")
        self.assertEqual(first[2], second[2])
        events = [json.loads(line) for line in self.proxy.log_path.read_text().splitlines()]
        self.assertEqual([e["cache"] for e in events], ["MISS", "HIT"])
        self.assertIn("timestamp", events[0])

    def test_concurrent_clients(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            responses = list(pool.map(lambda i: self.request(self.url + f"/?client={i}"), range(16)))
        self.assertTrue(all(r[0] == 200 for r in responses))

    def test_blocklist_and_subdomains(self):
        for domain in ("blocked.example", "sub.blocked.example"):
            self.assertEqual(self.request(f"http://{domain}/")[0], 403)

    def test_bad_targets_and_methods(self):
        for url in ("/relative", "https://example.com/", "http://user:pass@example.com/", "http://example.com:99999/"):
            self.assertEqual(self.request(url)[0], 400)
        self.assertEqual(self.request(method="POST")[0], 405)
        self.assertEqual(self.request("example.com:443", "CONNECT")[0], 501)

    def test_bypass(self):
        for headers in ({"Authorization": "Bearer demo"}, {"Cookie": "a=b"}, {"Cache-Control": "no-cache"}):
            self.assertEqual(self.request(headers=headers)[1]["X-Proxy-Cache"], "BYPASS")
        for _ in range(2):
            self.assertEqual(self.request(self.url + "/private")[1]["X-Proxy-Cache"], "MISS")

    def test_head(self):
        status, headers, body = self.request(method="HEAD")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"")
        self.assertGreater(int(headers["Content-Length"]), 0)

    def test_header_variants_do_not_share_cache(self):
        self.request(headers={"Accept": "text/plain"})
        self.assertEqual(self.request(headers={"Accept": "application/json"})[1]["X-Proxy-Cache"], "MISS")

    def test_raw_malformed_request(self):
        with socket.create_connection(self.proxy.server_address, timeout=3) as sock:
            sock.sendall(b"GET / HTTP/9.9\r\n\r\n")
            self.assertIn(b"505", sock.recv(4096))

    def test_worker_admission_limit(self):
        self.assertTrue(self.proxy.slots.acquire(blocking=False))
        # Fill all admission slots without relying on timing of slow clients.
        acquired = 1
        try:
            while self.proxy.slots.acquire(blocking=False):
                acquired += 1
            self.assertEqual(self.request()[0], 503)
        finally:
            for _ in range(acquired):
                self.proxy.slots.release()

    def test_size_limit_and_unreachable(self):
        self.assertEqual(self.request(self.url + "/large")[0], 502)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            unused_port = sock.getsockname()[1]
        self.assertIn(self.request(f"http://127.0.0.1:{unused_port}/")[0], (502, 504))

    def test_private_destinations(self):
        self.proxy.config["allow_private_destinations"] = False
        self.assertEqual(self.request()[0], 403)

    def test_rate_limit(self):
        self.proxy.limiter = RateLimiter(1, 60)
        self.assertEqual(self.request()[0], 200)
        self.assertEqual(self.request()[0], 429)

    def test_timeout(self):
        self.origin.delay = 0.1
        self.proxy.config["timeout"] = 0.01
        self.assertEqual(self.request()[0], 504)

    def test_body_rejected(self):
        self.assertEqual(self.request(headers={"Content-Length": "10"})[0], 400)


if __name__ == "__main__":
    unittest.main()
