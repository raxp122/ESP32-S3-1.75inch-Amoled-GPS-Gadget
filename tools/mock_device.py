#!/usr/bin/env python3
"""A fake AmuletOS on the PC: serves main/www/upload.html (or wifi.html) with the same API as
main/web.c and stores the uploads in a folder, so the pages can be worked on without the board.

    python3 tools/mock_device.py [--port 8466] [--dir /tmp/amulet-sd] [--wifi]

Then open http://localhost:8466/ ; the PIN is 1234. Files land in <dir>/amulet/ like on the card.
"""
import argparse, json, os, re, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIN = "1234"


def safe(name):
    """Same rules as media_safe_name() + the extension check of web.c"""
    m = re.match(r"^(.*)\.(jpg|jpeg|amj)$", name, re.I)
    if not m:
        return None
    base = re.sub(r"[^A-Za-z0-9_-]+", "_", m.group(1)).strip("_")[:40]
    if not base:
        return None
    return base + (".amj" if m.group(2).lower() == "amj" else ".jpg")


class H(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body)
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _auth(self):
        if self.headers.get("X-Pin") == PIN:
            return True
        self._send(403, "PIN", "text/plain")
        return False

    def _q(self):
        return dict(urllib.parse.parse_qsl(urllib.parse.urlparse(self.path).query))

    def _media(self):
        out = []
        for f in sorted(os.listdir(self.server.media), key=lambda f: -os.path.getmtime(os.path.join(self.server.media, f))):
            p = os.path.join(self.server.media, f)
            if os.path.isfile(p) and safe(f) == f:
                out.append({"f": f, "k": 2 if f.endswith(".amj") else 1, "s": os.path.getsize(p)})
        return out

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/":
            page = "wifi.html" if self.server.wifi else "upload.html"
            with open(os.path.join(ROOT, "main", "www", page), "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
        if path == "/api/info":
            st = os.statvfs(self.server.media)
            return self._send(200, {"name": "AmuletOS", "version": "0.1.0-mock", "mode": "wifi" if self.server.wifi else "upload",
                                    "sd": True, "free": st.f_bavail * st.f_frsize, "total": st.f_blocks * st.f_frsize,
                                    "count": len(self._media()), "size": 466, "thumb": 240})
        if path == "/api/scan":
            return self._send(200, [{"s": "Casa", "r": -48, "l": 1}, {"s": "Casa-5G", "r": -61, "l": 1}, {"s": "Bar", "r": -80, "l": 0}])
        if path == "/api/list" and self._auth():
            return self._send(200, self._media())
        if path == "/api/thumb" and self._auth():
            f = safe(self._q().get("f", ""))
            p = f and os.path.join(self.server.thumbs, f.rsplit(".", 1)[0] + ".jpg")
            if not p or not os.path.exists(p):
                return self._send(404, "no thumbnail", "text/plain")
            with open(p, "rb") as fh:
                return self._send(200, fh.read(), "image/jpeg")
        if not self.path.startswith("/api/"):
            return self._send(404, "", "text/plain")

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        if path == "/api/wifi":
            q = dict(urllib.parse.parse_qsl(body.decode()))
            print("wifi credentials:", q.get("s"), "/", "*" * len(q.get("p", "")))
            return self._send(200, {"ok": True})
        if not self._auth():
            return
        if path == "/api/auth":
            return self._send(200, {"ok": True})
        q = self._q()
        f = safe(q.get("f", ""))
        if not f:
            return self._send(400, "name", "text/plain")
        if path == "/api/upload":
            if q.get("t") == "thumb":
                p = os.path.join(self.server.thumbs, f.rsplit(".", 1)[0] + ".jpg")
            else:
                p = os.path.join(self.server.media, f)
            with open(p, "wb") as fh:
                fh.write(body)
            print("received", p, len(body), "bytes")
            return self._send(200, {"ok": True})
        if path == "/api/delete":
            p = os.path.join(self.server.media, f)
            if not os.path.exists(p):
                return self._send(404, "not found", "text/plain")
            os.remove(p)
            t = os.path.join(self.server.thumbs, f.rsplit(".", 1)[0] + ".jpg")
            if os.path.exists(t):
                os.remove(t)
            return self._send(200, {"ok": True})
        self._send(404, "", "text/plain")

    def log_message(self, *a):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8466)
    ap.add_argument("--dir", default="/tmp/amulet-sd")
    ap.add_argument("--wifi", action="store_true", help="serve the Wi-Fi setup page instead")
    a = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    srv.media = os.path.join(a.dir, "amulet")
    srv.thumbs = os.path.join(srv.media, ".thumb")
    srv.wifi = a.wifi
    os.makedirs(srv.thumbs, exist_ok=True)
    print("AmuletOS mock on http://localhost:%d/  (PIN %s, files in %s)" % (a.port, PIN, srv.media))
    srv.serve_forever()


if __name__ == "__main__":
    main()
