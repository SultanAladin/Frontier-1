#!/usr/bin/env python3
"""Preview server for the Component Viewer on 0.0.0.0 (default port 8095).

Serves the repository root with no-store caching (edits show on reload); / redirects into the studio.  Usage: python3 Experimental/ComponentViewer/Serve.py [port]
"""
import functools
import http.server
import pathlib
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".js": "text/javascript", ".mjs": "text/javascript", ".facet": "application/octet-stream"}

    def redirect_root(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/ComponentViewer/")
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
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8095
    root = pathlib.Path(__file__).resolve().parents[2]
    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), functools.partial(Handler, directory=str(root)))
    print(f"Component Viewer on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()
