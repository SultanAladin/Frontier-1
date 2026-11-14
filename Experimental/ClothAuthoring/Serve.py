"""Frontier Cloth Authoring (XPBD Top-3) preview server. Port 8087."""
import http.server
import socketserver
import sys
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8088
ROOT = Path(__file__).resolve().parent.parent.parent
DEMO_PATH = "/Experimental/ClothAuthoring/"


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(ROOT), **kw)

    def do_GET(self):
        # Make the preview root open the demo, not the repository file listing.
        if self.path == "/":
            self.send_response(302)
            self.send_header("Location", DEMO_PATH)
            self.end_headers()
            return
        super().do_GET()

    def log_message(self, fmt, *args):
        sys.stderr.write("[ClothAuthoring] %s\n" % (fmt % args))


if __name__ == "__main__":
    with ReusableTCPServer(("0.0.0.0", PORT), Handler) as httpd:
        print(f"Cloth Authoring at http://0.0.0.0:{PORT}{DEMO_PATH}")
        httpd.serve_forever()
