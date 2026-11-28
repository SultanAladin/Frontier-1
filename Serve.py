#!/usr/bin/env python3
"""Static HTTP server bound to 0.0.0.0 for Arena Live Preview with clean MIME types for WebGPU WGSL, JS modules, IES, and FCT1 binaries."""

import http.server
import mimetypes
import os
import socketserver
import sys

mimetypes.add_type("application/javascript", ".js")
mimetypes.add_type("application/javascript", ".mjs")
mimetypes.add_type("text/plain", ".wgsl")
mimetypes.add_type("text/plain", ".ies")
mimetypes.add_type("application/octet-stream", ".face")
mimetypes.add_type("font/woff2", ".woff2")


class PreviewRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store, must-revalidate")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stdout.write("[SurfelGI-Preview] " + (fmt % args) + "\n")
        sys.stdout.flush()


class ReusableTcpServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else int(os.environ.get("PORT", "8080"))
    root_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(root_dir)
    with ReusableTcpServer(("0.0.0.0", port), PreviewRequestHandler) as httpd:
        sys.stdout.write(f"[SurfelGI-Preview] Listening on http://0.0.0.0:{port}\n")
        sys.stdout.flush()
        httpd.serve_forever()


if __name__ == "__main__":
    main()
