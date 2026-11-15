#!/usr/bin/env python3
"""Standalone live preview server for Experimental/ProjectBrowser on 0.0.0.0:8099."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[2]
BROWSER_DIR = Path(__file__).resolve().parent


class ProjectBrowserHandler(SimpleHTTPRequestHandler):
    def translate_path(self, path):
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean in ("/", "/index.html"):
            return str(BROWSER_DIR / "index.html")
        # Support root-relative assets when opened directly at "/"
        rel = clean.lstrip("/")
        candidate = BROWSER_DIR / rel
        if candidate.exists():
            return str(candidate)
        # Support "../FrontierWorldMirror/..." resolving to "/FrontierWorldMirror/..." when opened at "/"
        if clean.startswith("/FrontierWorldMirror"):
            return str(REPO_ROOT / "Experimental" / rel)
        if clean.startswith("/ProjectBrowser"):
            return str(REPO_ROOT / "Experimental" / rel)
        return super().translate_path(path)

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    handler = partial(ProjectBrowserHandler, directory=str(REPO_ROOT))
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    print(f"Frontier Project Browser serving on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()
