import argparse
import concurrent.futures
import csv
import socket
import time
import unittest

from harness import Lab

BINARY = "./cnproxy"


class ProxyTests(unittest.TestCase):
    def setUp(self):
        self.lab = Lab(BINARY, ["--timeout", "1"])

    def tearDown(self):
        self.lab.close()

    def test_cache(self):
        first = self.lab.request()
        second = self.lab.request()
        self.assertEqual(first[0], 200)
        self.assertEqual(first[2], second[2])
        self.assertEqual(first[1]["X-Proxy-Cache"].strip(), "MISS")
        self.assertEqual(second[1]["X-Proxy-Cache"].strip(), "HIT")

    def test_concurrency(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda i: self.lab.request(f"/?id={i}"), range(16)))
        self.assertTrue(all(r[0] == 200 for r in results))

    def test_blocklist(self):
        for host in ("blocked.example", "sub.blocked.example"):
            self.assertEqual(self.lab.request(target=f"http://{host}/")[0], 403)

    def test_targets(self):
        for url in ("/relative", "https://example.com/", "http://u:p@example.com/", "http://example.com:99999/"):
            self.assertEqual(self.lab.request(target=url)[0], 400)

    def test_methods(self):
        self.assertEqual(self.lab.request(method="POST")[0], 405)
        self.assertEqual(self.lab.request(method="CONNECT", target="example.com:443")[0], 501)

    def test_head(self):
        result = self.lab.request(method="HEAD")
        self.assertEqual(result[0], 200)
        self.assertEqual(result[2], b"")
        self.assertGreater(int(result[1]["Content-Length"]), 0)

    def test_bypass(self):
        for headers in ({"Authorization": "demo"}, {"Cookie": "a=b"}, {"Range": "bytes=0-1"}, {"Cache-Control": "no-cache"}):
            self.assertEqual(self.lab.request(headers=headers)[1]["X-Proxy-Cache"].strip(), "BYPASS")

    def test_not_stored(self):
        for path in ("/private", "/cookie", "/vary"):
            for _ in range(2):
                self.assertEqual(self.lab.request(path)[1]["X-Proxy-Cache"].strip(), "MISS")

    def test_header_variants(self):
        self.lab.request(headers={"Accept": "text/plain"})
        self.assertEqual(self.lab.request(headers={"Accept": "application/json"})[1]["X-Proxy-Cache"].strip(), "MISS")

    def test_hop_headers(self):
        self.assertNotIn("X-Internal", self.lab.request("/hop")[1])

    def test_binary_body(self):
        self.assertEqual(self.lab.request("/binary")[2], bytes(range(256)))
        self.assertEqual(self.lab.request("/binary")[2], bytes(range(256)))

    def test_bad_framing(self):
        self.assertEqual(self.lab.request(headers={"Content-Length": "2"})[0], 400)
        with socket.create_connection(("127.0.0.1", self.lab.port)) as sock:
            sock.sendall(f"GET {self.lab.url}/ HTTP/1.1\r\nHost: local\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n".encode())
            self.assertIn(b"400", sock.recv(4096))

    def test_large_and_truncated(self):
        self.assertEqual(self.lab.request("/large")[0], 502)
        self.assertEqual(self.lab.request("/truncated")[0], 502)

    def test_chunked_is_rejected(self):
        self.assertEqual(self.lab.request("/chunked")[0], 502)

    def test_unreachable_origin(self):
        from harness import free_port
        self.assertEqual(self.lab.request(target=f"http://127.0.0.1:{free_port()}/")[0], 502)

    def test_private_destination_default(self):
        self.lab.close()
        self.lab = Lab(BINARY, allow_private=False)
        self.assertEqual(self.lab.request()[0], 403)

    def test_timeout(self):
        self.assertEqual(self.lab.request("/slow")[0], 504)

    def test_log(self):
        self.lab.request()
        with self.lab.log.open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(rows[-1]["cache"], "MISS")
        self.assertEqual(rows[-1]["destination"], "127.0.0.1")
        self.assertEqual(rows[-1]["status"], "200")

    def restart(self, options):
        self.lab.close()
        self.lab = Lab(BINARY, options)

    def test_rate_limit(self):
        self.restart(["--rate", "1", "--window", "1"])
        # The readiness probe is malformed and does not consume quota.
        self.assertEqual(self.lab.request()[0], 200)
        self.assertEqual(self.lab.request()[0], 429)
        time.sleep(1.05)
        self.assertEqual(self.lab.request()[0], 200)

    def test_expiry(self):
        self.restart(["--ttl", "1"])
        self.lab.request()
        time.sleep(1.05)
        self.assertEqual(self.lab.request()[1]["X-Proxy-Cache"].strip(), "MISS")

    def test_eviction(self):
        for i in range(17):
            self.lab.request(f"/?cache={i}")
        self.assertEqual(self.lab.request("/?cache=0")[1]["X-Proxy-Cache"].strip(), "MISS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=BINARY)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary
    unittest.main(argv=[__file__, *remaining])
