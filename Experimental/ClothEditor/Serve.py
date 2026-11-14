#!/usr/bin/env python3
"""
Frontier Cloth & Tailoring Atelier Preview Server
Serves Experimental/ClothEditor/ at / and Experimental/UVEditor/ at /UVEditor/*
Binds 0.0.0.0:8084 with permissive host/origin/CORS headers for live browser preview.
"""

import argparse
import mimetypes
import os
import posixpath
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLOTH_DIR = os.path.join(REPO_ROOT, "Experimental", "ClothEditor")
UV_DIR = os.path.join(REPO_ROOT, "Experimental", "UVEditor")

mimetypes.add_type("application/javascript", ".js")
mimetypes.add_type("application/javascript", ".mjs")
mimetypes.add_type("text/css", ".css")
mimetypes.add_type("application/octet-stream", ".bin")
mimetypes.add_type("font/woff2", ".woff2")


class ClothStudioRequestHandler(SimpleHTTPRequestHandler):
    def do_GET(self) -> None:
        for hdr in ("If-Modified-Since", "If-None-Match"):
            if hdr in self.headers:
                del self.headers[hdr]
        super().do_GET()

    def translate_path(self, path: str) -> str:
        clean = posixpath.normpath(unquote(urlsplit(path).path))
        if clean in ("", "/"):
            return os.path.join(CLOTH_DIR, "index.html")
        if clean == "/UVEditor" or clean.startswith("/UVEditor/"):
            rel = clean[len("/UVEditor"):].lstrip("/")
            return os.path.join(UV_DIR, rel)
        return os.path.join(CLOTH_DIR, clean.lstrip("/"))

    def end_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def log_message(self, fmt: str, *args) -> None:
        print(f"[ClothStudio] {self.address_string()} - {fmt % args}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve Frontier Cloth Atelier")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8084)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), ClothStudioRequestHandler)
    print(f"Frontier Cloth Atelier listening on http://{args.host}:{args.port}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
