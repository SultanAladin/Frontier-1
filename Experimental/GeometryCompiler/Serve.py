#!/usr/bin/env python3
"""Standalone preview server for Experimental/GeometryCompiler (and shared StudioIconSet) on 0.0.0.0:8094."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[2]


class GeometryCompilerHandler(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript; charset=utf-8",
        ".mjs": "text/javascript; charset=utf-8",
        ".wgsl": "text/plain; charset=utf-8",
        ".woff2": "font/woff2",
        ".svg": "image/svg+xml",
        ".json": "application/json; charset=utf-8",
    }

    def log_message(self, fmt, *args):
        return

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        path_only, _, query = self.path.partition("?")
        if path_only in ("/", "/index.html"):
            target = "/Experimental/GeometryCompiler/" + (f"?{query}" if query else "")
            self.send_response(302)
            self.send_header("Location", target)
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8094
    handler = partial(GeometryCompilerHandler, directory=str(REPO_ROOT))
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    print(f"Frontier Geometry Compiler serving on http://0.0.0.0:{port}/Experimental/GeometryCompiler/", flush=True)
    server.serve_forever()
