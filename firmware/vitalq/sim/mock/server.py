#!/usr/bin/env python3
"""Mock VitalQ cloud — receives the real HTTP requests cloud_client emits.

Usage: python3 server.py [--port 8410] [--fail N] [--drop-every K]
Writes each received payload as one JSON line to <outdir>/received.jsonl.
Exit-check file <outdir>/stats.json is updated per request.
"""
import argparse, json, os, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime, timezone

STATS = {"requests": 0, "batches": 0, "records": 0, "errors": 0,
         "paths": {}, "last_auth": None, "first_ts": None, "last_ts": None}


def make_handler(outdir, fail_after, drop_every):
    class H(BaseHTTPRequestHandler):
        req_count = [0]

        def _stats(self):
            p = os.path.join(outdir, "stats.json")
            with open(p, "w") as f:
                json.dump(STATS, f, indent=1)

        def _handle(self, method):
            n = req_count[0] = req_count[0] + 1
            STATS["requests"] += 1
            STATS["paths"][self.path] = STATS["paths"].get(self.path, 0) + 1
            STATS["last_auth"] = self.headers.get("Authorization")
            now = datetime.now(timezone.utc).isoformat()
            STATS["first_ts"] = STATS["first_ts"] or now
            STATS["last_ts"] = now

            if drop_every and n % drop_every == 0:
                self.connection.close()          # simulate conn reset
                return
            if fail_after and n > fail_after:
                self.send_response(500)
                self.end_headers(); self.wfile.write(b'{"error":"mock 500"}')
                return

            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else b""
            try:
                doc = json.loads(body) if body else {}
            except Exception:
                STATS["errors"] += 1
                self.send_response(400); self.end_headers(); return

            recs = doc.get("records") or doc.get("batch") or [doc]
            STATS["batches"] += 1
            STATS["records"] += len(recs) if isinstance(recs, list) else 1
            with open(os.path.join(outdir, "received.jsonl"), "a") as f:
                f.write(json.dumps({"path": self.path, "doc": doc}) + "\n")

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"ok": True,
                                         "accepted": STATS["records"]}).encode())
            self._stats()              # per-request exit-check snapshot

        do_POST = lambda s: H._handle(s, "POST")
        do_PUT = lambda s: H._handle(s, "PUT")
        do_GET = lambda s: H._handle(s, "GET")

        def log_message(self, *a):
            pass
    req_count = H.req_count
    return H


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8410)
    ap.add_argument("--outdir", default=".")
    ap.add_argument("--fail-after", type=int, default=0)
    ap.add_argument("--drop-every", type=int, default=0)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), make_handler(
        a.outdir, a.fail_after, a.drop_every))
    print(f"mock-cloud on 127.0.0.1:{a.port} outdir={a.outdir}", flush=True)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
