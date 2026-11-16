#!/usr/bin/env python3
"""Standalone HTTP preview server for Experimental Remastered Force Field + Wind Studio on 0.0.0.0:8085."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class ForceFieldHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/ForceFieldEditor/")
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    handler = partial(ForceFieldHandler, directory=str(root))
    server = ThreadingHTTPServer(("0.0.0.0", 8085), handler)
    print(f"Force Field + Wind Studio serving {root} on http://0.0.0.0:8085/", flush=True)
    server.serve_forever()
