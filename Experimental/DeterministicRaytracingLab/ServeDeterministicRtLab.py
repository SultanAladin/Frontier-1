#!/usr/bin/env python3
"""Live HTTP preview server for Experimental/DeterministicRaytracingLab on 0.0.0.0:8097."""
import http.server
import os
import socketserver

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAB_ROOT = os.path.abspath(os.path.dirname(__file__))
PORT = int(os.environ.get("PORT", "8097"))


class DeterministicLabHandler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean.startswith("/Exhibits/"):
            return os.path.join(REPO_ROOT, clean.lstrip("/"))
        rel = clean.lstrip("/") or "index.html"
        return os.path.join(LAB_ROOT, rel)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, must-revalidate")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()


class ReusableTcpServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    with ReusableTcpServer(("0.0.0.0", PORT), DeterministicLabHandler) as httpd:
        print(f"[DeterministicRaytracingLab] Serving {LAB_ROOT} on http://0.0.0.0:{PORT}", flush=True)
        httpd.serve_forever()
