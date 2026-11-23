#!/usr/bin/env python3
"""Serve the Fluid Studio reference HTML on 0.0.0.0 with no-cache headers so edits appear immediately."""
import http.server
import os
import sys

Port = int(sys.argv[1]) if len(sys.argv) > 1 else 8098
Root = os.path.dirname(os.path.abspath(__file__))


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *Arguments, **Keywords):
        super().__init__(*Arguments, directory=Root, **Keywords)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, must-revalidate")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        super().end_headers()

    def log_message(self, Format, *Arguments):
        sys.stderr.write("[FluidStudio] " + (Format % Arguments) + "\n")


if __name__ == "__main__":
    with http.server.ThreadingHTTPServer(("0.0.0.0", Port), NoCacheHandler) as Server:
        print(f"Fluid Studio → http://0.0.0.0:{Port}/", flush=True)
        try:
            Server.serve_forever()
        except KeyboardInterrupt:
            pass
