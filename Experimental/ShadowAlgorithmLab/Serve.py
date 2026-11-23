#!/usr/bin/env python3
"""Live HTTP server for Experimental/ShadowAlgorithmLab bound to 0.0.0.0:8098."""

import http.server
import os
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8098
ROOT = os.path.dirname(os.path.abspath(__file__))


class ShadowLabRequestHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "application/javascript; charset=utf-8",
        ".mjs": "application/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".html": "text/html; charset=utf-8",
        ".bin": "application/octet-stream",
        ".woff2": "font/woff2",
    }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


class ReusableThreadingTcpServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    with ReusableThreadingTcpServer(("0.0.0.0", PORT), ShadowLabRequestHandler) as httpd:
        print(f"[ShadowAlgorithmLab] Serving {ROOT} on http://0.0.0.0:{PORT}", flush=True)
        httpd.serve_forever()
