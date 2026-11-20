#!/usr/bin/env python3
"""Run the standalone Vehicle Lab preview on 0.0.0.0:8086."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class VehicleLabHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/VehicleLab/")
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    handler = partial(VehicleLabHandler, directory=str(root))
    server = ThreadingHTTPServer(("0.0.0.0", 8086), handler)
    print(f"Vehicle Lab serving on http://0.0.0.0:8086/", flush=True)
    server.serve_forever()
