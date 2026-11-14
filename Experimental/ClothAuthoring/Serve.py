"""Frontier Cloth Authoring (XPBD Top-3) static server. Port 8087."""
import http.server
import socketserver
import sys
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8087
ROOT = Path(__file__).resolve().parent.parent.parent

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(ROOT), **kw)
    def log_message(self, fmt, *args):
        sys.stderr.write("[ClothAuthoring] %s\n" % (fmt % args))

if __name__ == "__main__":
    with socketserver.TCPServer(("0.0.0.0", PORT), Handler) as httpd:
        print(f"Cloth Authoring on http://0.0.0.0:{PORT}/Experimental/ClothAuthoring/")
        httpd.serve_forever()
