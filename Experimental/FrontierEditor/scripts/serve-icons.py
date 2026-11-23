"""Serve the standalone icon gallery as its own preview, without changing the editor."""
import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]

class IconHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def end_headers(self):
        # Gallery HTML and SVG exports change in place during design iteration.
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def send_head(self):
        # Ignore validators so a refreshed preview never reuses stale artwork.
        for header in ('If-Modified-Since', 'If-None-Match'):
            if header in self.headers:
                del self.headers[header]
        return super().send_head()

    def do_GET(self):
        if urlsplit(self.path).path == '/':
            self.path = '/icons.html'
        super().do_GET()

    def do_HEAD(self):
        if urlsplit(self.path).path == '/':
            self.path = '/icons.html'
        super().do_HEAD()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=5174)
    port = parser.parse_args().port
    print(f'Custom SVG icon gallery listening on 0.0.0.0:{port}', flush=True)
    ThreadingHTTPServer(('0.0.0.0', port), IconHandler).serve_forever()
