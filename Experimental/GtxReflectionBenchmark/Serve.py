#!/usr/bin/env python3
"""Live HTTP server for Experimental/GtxReflectionBenchmark bound to 0.0.0.0:8096."""

import http.server
import os
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8096
ROOT = os.path.dirname(os.path.abspath(__file__))


class BenchmarkRequestHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "application/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".html": "text/html; charset=utf-8",
        ".bin": "application/octet-stream",
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
    with ReusableThreadingTcpServer(("0.0.0.0", PORT), BenchmarkRequestHandler) as httpd:
        print(f"[GtxReflectionBenchmark] Serving {ROOT} on http://0.0.0.0:{PORT}", flush=True)
        httpd.serve_forever()
