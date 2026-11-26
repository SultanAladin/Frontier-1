#!/usr/bin/env python3
"""Live preview server for 'Experimental/Project browser' on 0.0.0.0:8770 (no-store, so edits always show)."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class NoStoreHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()


if __name__ == "__main__":
    Handler = partial(NoStoreHandler, directory=str(Path(__file__).resolve().parent))
    ThreadingHTTPServer(("0.0.0.0", 8770), Handler).serve_forever()
