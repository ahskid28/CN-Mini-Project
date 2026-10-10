"""Deterministic origin for demos, tests and benchmarks."""
import argparse
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Origin(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        time.sleep(self.server.delay)
        body = b"Computer Networks proxy demonstration\n" * (300 if self.path == "/large" else 1)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Cache-Control", "no-store" if self.path == "/private" else "public, max-age=30")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(body)
            except OSError:
                pass  # Timeout tests intentionally disconnect early.

    do_HEAD = do_GET


def make_origin(delay=0.05):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Origin)
    server.delay = delay
    server.daemon_threads = True
    return server


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--delay", type=float, default=0.05)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Origin)
    server.delay = args.delay
    print(f"Demo origin: http://127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
