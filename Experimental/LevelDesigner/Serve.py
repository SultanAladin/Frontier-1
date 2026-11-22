#!/usr/bin/env python3
"""Preview server for Level Designer on 0.0.0.0 (default port 8092).

Serves the repository root with no-store caching; / redirects straight into /Experimental/LevelDesigner/.
Usage: python3 Experimental/LevelDesigner/Serve.py [port]
"""
import functools
import http.server
import pathlib
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".facet": "application/octet-stream",
    }

    def redirect_root(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/LevelDesigner/")
            self.end_headers()
            return True
        return False

    def do_GET(self):
        if not self.redirect_root():
            super().do_GET()

    def do_HEAD(self):
        if not self.redirect_root():
            super().do_HEAD()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8092
    root = pathlib.Path(__file__).resolve().parents[2]
    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), functools.partial(Handler, directory=str(root)))
    print(f"Level Designer on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()
