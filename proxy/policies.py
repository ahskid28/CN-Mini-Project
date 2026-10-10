"""Thread-safe bounded cache and sliding-window per-client limiter."""
import time
from collections import OrderedDict, deque
from threading import Lock


class Cache:
    def __init__(self, max_bytes, clock=time.monotonic):
        self.max_bytes = max_bytes
        self.clock = clock
        self.items = OrderedDict()
        self.size = 0
        self.lock = Lock()

    def get(self, key):
        with self.lock:
            item = self.items.get(key)
            if item is None:
                return None
            expires, value, size = item
            if expires <= self.clock():
                self.items.pop(key)
                self.size -= size
                return None
            self.items.move_to_end(key)
            return value

    def put(self, key, value, ttl):
        size = len(value[2]) + sum(len(k) + len(v) for k, v in value[1]) + 128
        if size > self.max_bytes or ttl <= 0:
            return
        with self.lock:
            if key in self.items:
                self.size -= self.items.pop(key)[2]
            while self.items and self.size + size > self.max_bytes:
                self.size -= self.items.popitem(last=False)[1][2]
            self.items[key] = (self.clock() + ttl, value, size)
            self.size += size


class RateLimiter:
    def __init__(self, limit, window, clock=time.monotonic):
        self.limit, self.window, self.clock = limit, window, clock
        self.clients = {}
        self.lock = Lock()

    def allow(self, client):
        if self.limit == 0:
            return True
        now = self.clock()
        with self.lock:
            # Reclaim inactive clients rather than keeping every historical IP.
            for key in list(self.clients):
                if not self.clients[key] or self.clients[key][-1] <= now - self.window:
                    del self.clients[key]
            queue = self.clients.setdefault(client, deque())
            while queue and queue[0] <= now - self.window:
                queue.popleft()
            if len(queue) >= self.limit:
                return False
            queue.append(now)
            return True
