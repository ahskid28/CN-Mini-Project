"""Test-only Python utilities; the proxy itself is entirely C."""
import http.client
import socket
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Origin(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            self.wfile.write(b"4\r\ndemo\r\n0\r\n\r\n")
            return
        if self.path.startswith("/slow"):
            time.sleep(2)
        else:
            time.sleep(self.server.delay)
        body = b"C proxy demonstration\n" if self.path != "/large" else b"x" * (1024 * 1024 + 1)
        if self.path == "/binary":
            body = bytes(range(256))
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body) + (10 if self.path == "/truncated" else 0)))
        self.send_header("Cache-Control", "no-store" if self.path == "/private" else "public, max-age=30")
        if self.path == "/cookie":
            self.send_header("Set-Cookie", "demo=1")
        if self.path == "/vary":
            self.send_header("Vary", "Accept")
        if self.path == "/hop":
            self.send_header("Connection", "X-Internal")
            self.send_header("X-Internal", "remove")
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(body)
            except OSError:
                pass

    do_HEAD = do_GET


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Lab:
    def __init__(self, binary, options=(), delay=0.02, allow_private=True):
        self.temp = tempfile.TemporaryDirectory()
        self.log = Path(self.temp.name) / "traffic.csv"
        self.port = free_port()
        self.origin = ThreadingHTTPServer(("127.0.0.1", 0), Origin)
        self.origin.daemon_threads = True
        self.origin.delay = delay
        self.thread = threading.Thread(target=self.origin.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.origin.server_port}"
        self.process = subprocess.Popen([str(Path(binary).resolve()), "--port", str(self.port),
            "--rate", "0", *( ["--allow-private"] if allow_private else [] ), "--log", str(self.log), *options],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for _ in range(100):
            if self.process.poll() is not None:
                raise RuntimeError(self.process.communicate()[1].decode())
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.05):
                    return
            except OSError:
                time.sleep(0.05)
        self.close()
        raise RuntimeError("Proxy failed to start")

    def request(self, path="/", method="GET", headers=None, target=None, direct=False):
        port = self.origin.server_port if direct else self.port
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        try:
            conn.request(method, path if direct else target or self.url + path, headers=headers or {})
            response = conn.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            conn.close()

    def close(self):
        self.process.terminate()
        try:
            self.process.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate()
        self.origin.shutdown()
        self.origin.server_close()
        self.thread.join()
        self.temp.cleanup()
