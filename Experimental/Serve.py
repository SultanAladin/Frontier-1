#!/usr/bin/env python3
"""Serve all 8 Experimental reference studios from repository root on 0.0.0.0:8080."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys


class ExperimentalStudioHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/")
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    repo_root = Path(__file__).resolve().parents[1]
    handler = partial(ExperimentalStudioHandler, directory=str(repo_root))
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    print(f"Frontier Experimental Studios serving {repo_root} on http://0.0.0.0:{port}/Experimental/", flush=True)
    server.serve_forever()
