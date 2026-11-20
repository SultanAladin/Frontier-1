#!/usr/bin/env python3
"""Serve the Frontier Cloth Atelier static ES module app cleanly inside sandbox live previews."""

from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from functools import partial
import os
import sys


class ClothAtelierHandler(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        '.js': 'application/javascript; charset=utf-8',
        '.mjs': 'application/javascript; charset=utf-8',
        '.css': 'text/css; charset=utf-8',
        '.svg': 'image/svg+xml',
        '.woff2': 'font/woff2',
        '.bin': 'application/octet-stream',
        '.json': 'application/json; charset=utf-8',
    }

    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', '*')
        self.send_header('Cross-Origin-Resource-Policy', 'cross-origin')
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else int(os.environ.get('PORT', '8092'))
    root_dir = os.path.dirname(os.path.abspath(__file__))
    handler = partial(ClothAtelierHandler, directory=root_dir)
    ThreadingHTTPServer.allow_reuse_address = True
    server = ThreadingHTTPServer(('0.0.0.0', port), handler)
    print(f'Frontier Cloth Atelier listening on http://0.0.0.0:{port}/', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
