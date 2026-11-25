"""Frontier Experimental Remastered Studio standalone preview server on 0.0.0.0:8087."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import mimetypes
import os

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)

mimetypes.add_type('application/javascript', '.js')
mimetypes.add_type('application/javascript', '.mjs')
mimetypes.add_type('font/woff2', '.woff2')
mimetypes.add_type('application/octet-stream', '.sdfspace')


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ('/', '/index.html'):
            self.send_response(302)
            self.send_header('Location', 'Experimental%20Remastered/TerrainWorldEditor/')
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()


if __name__ == '__main__':
    print('Serving Frontier SDF Terrain & Clay Studios on http://0.0.0.0:8087/')
    ThreadingHTTPServer(('0.0.0.0', 8087), Handler).serve_forever()
