#!/usr/bin/env python3
"""Serve the third Component Viewer prototype (default port 8096)."""
import functools
import http.server
import pathlib
import sys

class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, '.js': 'text/javascript', '.mjs': 'text/javascript', '.facet': 'application/octet-stream'}
    def redirect_root(self):
        if self.path not in ('/', '/index.html'):
            return False
        self.send_response(302)
        self.send_header('Location', '/Experimental/ComponentViewer/AssetViewer.html')
        self.end_headers()
        return True
    def do_GET(self):
        if not self.redirect_root():
            super().do_GET()
    def do_HEAD(self):
        if not self.redirect_root():
            super().do_HEAD()
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

if __name__ == '__main__':
    root = pathlib.Path(__file__).resolve().parents[2]
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8096
    server = http.server.ThreadingHTTPServer(('0.0.0.0', port), functools.partial(Handler, directory=str(root)))
    print(f'Asset Viewer on http://0.0.0.0:{port}/', flush=True)
    server.serve_forever()
