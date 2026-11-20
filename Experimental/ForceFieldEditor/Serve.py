#!/usr/bin/env python3
# ============================================================================================================================================
#                                                        SERVE.PY
# ============================================================================================================================================
# Preview server for the ForceField Studio: binds 0.0.0.0 so the hosted preview can reach it, and serves the studio folder from the repository
# root so the shared Experimental assets stay on relative URLs.

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys


class ForceFieldStudioHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def log_message(self, fmt, *args):
        return


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8094
    repo_root = Path(__file__).resolve().parents[2]
    studio = "Experimental/ForceFieldEditor"
    handler = partial(ForceFieldStudioHandler, directory=str(repo_root))
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    print(f"ForceField Studio serving {repo_root} on http://0.0.0.0:{port}/{studio}/index.html", flush=True)
    server.serve_forever()
