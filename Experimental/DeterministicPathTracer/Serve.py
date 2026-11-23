#=============================================================================================================================================
#                                                                 SERVE.PY
#=============================================================================================================================================
# 🧩 Preview server for the deterministic path tracer (serves this folder only, never the repository root).
#
# Usage:  python3 Serve.py [port]        then open http://localhost:8087/
#
# 📝 WebGPU requires a secure context: localhost qualifies, plain-http LAN addresses do not.

from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import sys

PrototypeRoot = Path(__file__).resolve().parent


class PrototypeRequestRoute(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".wgsl": "text/plain", ".js": "text/javascript", ".facet": "application/octet-stream"}

    def __init__(self, *Arguments, **Keywords):
        super().__init__(*Arguments, directory=str(PrototypeRoot), **Keywords)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        super().end_headers()


if __name__ == "__main__":
    Port = int(sys.argv[1]) if len(sys.argv) > 1 else int(os.environ.get("PORT", "8087"))
    print(f"[DeterministicPathTracer] Serving {PrototypeRoot} on http://0.0.0.0:{Port}/", flush=True)
    ThreadingHTTPServer(("0.0.0.0", Port), PrototypeRequestRoute).serve_forever()
