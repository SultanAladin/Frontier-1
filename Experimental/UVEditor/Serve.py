"""Frontier UV Workspace preview server on 0.0.0.0 (serves Experimental/ only, never repo root)."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import sys

EXPERIMENTAL_ROOT = Path(__file__).resolve().parents[1]
os.chdir(EXPERIMENTAL_ROOT)


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ('/', '/index.html'):
            self.send_response(302)
            self.send_header('Location', '/UVEditor/index.html')
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else int(os.environ.get('PORT', '8085'))
    print(f'[UVWorkspace] Serving {EXPERIMENTAL_ROOT}/UVEditor on http://0.0.0.0:{port}/UVEditor/index.html', flush=True)
    ThreadingHTTPServer(('0.0.0.0', port), Handler).serve_forever()
