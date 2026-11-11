#!/usr/bin/env python3
"""Static HTTP server for Frontier Experimental Remastered Photometric & Light Studio (0.0.0.0:8084)."""

import functools
import http.server
import os
import pathlib
import socketserver


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("FRONTIER_PORT", "8084"))
STUDIO_PATH = "/Experimental/PhotometricEditor/"


class PhotometricStudioHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "application/javascript; charset=utf-8",
        ".mjs": "application/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".woff2": "font/woff2",
        ".ies": "text/plain; charset=utf-8",
        ".json": "application/json; charset=utf-8",
    }

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", STUDIO_PATH)
            self.end_headers()
            return
        super().do_GET()


def main():
    handler = functools.partial(PhotometricStudioHandler, directory=str(REPO_ROOT))
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer(("0.0.0.0", PORT), handler) as httpd:
        print(f"Frontier Photometric Studio serving {REPO_ROOT} at http://0.0.0.0:{PORT}{STUDIO_PATH}")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
