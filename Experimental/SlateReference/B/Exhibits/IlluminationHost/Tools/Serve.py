"""Preview server: never mix cached JS layouts with newer WGSL bindings."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()
ThreadingHTTPServer(('0.0.0.0', 8080), Handler).serve_forever()
