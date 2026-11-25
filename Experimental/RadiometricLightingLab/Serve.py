#!/usr/bin/env python3
"""Serve Experimental/RadiometricLightingLab on 0.0.0.0 for live browser preview."""

from __future__ import annotations

import argparse
import http.server
import os
import socketserver
from pathlib import Path

LAB_DIR = Path(__file__).resolve().parent
REPO_ROOT = LAB_DIR.parent.parent


class RadiometricLabHandler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path: str) -> str:
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean.startswith("/Experimental/") or clean.startswith("/Documentation/"):
            return str((REPO_ROOT / clean.lstrip("/")).resolve())
        rel = clean.lstrip("/") or "index.html"
        return str((LAB_DIR / rel).resolve())

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def log_message(self, fmt: str, *args) -> None:
        pass


class ReusableTcpServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve Frontier Radiometric Lighting Architecture Lab")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8088)
    args = parser.parse_args()

    os.chdir(LAB_DIR)
    with ReusableTcpServer((args.host, args.port), RadiometricLabHandler) as httpd:
        print(f"[RadiometricLightingLab] Listening on http://{args.host}:{args.port}/", flush=True)
        httpd.serve_forever()


if __name__ == "__main__":
    main()
