#!/usr/bin/env python3
"""ShadowBox editor server: static files + level JSON save/load API.

python3 Experimental/ShadowBoxEditor/Serve.py 8098

Levels are written to Experimental/ShadowBoxEditor/levels/ inside the
repository so they can be committed and read from GitHub.
"""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import re
import sys

EDITOR_DIR = Path(__file__).resolve().parent
LEVELS_DIR = EDITOR_DIR / "levels"
SLUG = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")


class ShadowBoxHandler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def _json(self, code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path_only = self.path.split("?", 1)[0]
        if path_only in ("/", "/index.html"):
            body = (EDITOR_DIR / "index.html").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path_only == "/api/levels":
            LEVELS_DIR.mkdir(parents=True, exist_ok=True)
            names = sorted(p.name for p in LEVELS_DIR.glob("*.json"))
            self._json(200, {"levels": names})
            return
        if path_only.startswith("/api/levels/"):
            name = path_only[len("/api/levels/") :]
            if not SLUG.match(name.removesuffix(".json")) or "/" in name:
                self._json(400, {"error": "bad name"})
                return
            if not name.endswith(".json"):
                name += ".json"
            target = LEVELS_DIR / name
            if not target.is_file():
                self._json(404, {"error": "not found"})
                return
            try:
                self._json(200, json.loads(target.read_text("utf-8")))
            except (OSError, json.JSONDecodeError) as exc:
                self._json(500, {"error": str(exc)})
            return
        super().do_GET()

    def do_POST(self):
        path_only = self.path.split("?", 1)[0]
        if path_only != "/api/save":
            self._json(404, {"error": "unknown route"})
            return
        try:
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0 or length > 16 * 1024 * 1024:
                raise ValueError("bad length")
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            name = str(payload.get("name") or "")
            slug = re.sub(r"[^a-z0-9_-]+", "-", name.lower()).strip("-")[:64]
            if not SLUG.match(slug or ""):
                raise ValueError("bad level name")
            doc = payload.get("doc")
            if not isinstance(doc, dict):
                raise ValueError("missing doc")
            LEVELS_DIR.mkdir(parents=True, exist_ok=True)
            target = LEVELS_DIR / f"{slug}.json"
            target.write_text(
                json.dumps(doc, indent=2, ensure_ascii=False) + "\n", "utf-8"
            )
            self._json(200, {"ok": True, "file": target.name})
        except (ValueError, json.JSONDecodeError, KeyError) as exc:
            self._json(400, {"error": str(exc)})
        except OSError as exc:
            self._json(500, {"error": str(exc)})

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8098
    handler = partial(ShadowBoxHandler, directory=str(EDITOR_DIR))
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    print(f"ShadowBox editor on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()
