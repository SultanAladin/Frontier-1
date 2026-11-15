#!/usr/bin/env python3
"""
Standalone self-hosted server for Experimental/ParticleEditor Final/.
Default port: 8097 on 0.0.0.0 (live-preview ready, no external CDN requests).
"""
import functools
import http.server
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

class WorkspaceHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        '.js': 'application/javascript; charset=utf-8',
        '.mjs': 'application/javascript; charset=utf-8',
        '.css': 'text/css; charset=utf-8',
        '.woff2': 'font/woff2',
        '.svg': 'image/svg+xml',
        '.ParticleSpace': 'text/plain; charset=utf-8',
        '.levelspace': 'text/plain; charset=utf-8',
    }

    def do_GET(self):
        if self.path in ('/', '/index.html'):
            self.send_response(302)
            self.send_header('Location', '/Experimental/ParticleEditor%20Final/')
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, must-revalidate')
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

    def log_message(self, format, *args):
        pass

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8097
    handler = functools.partial(WorkspaceHandler, directory=REPO_ROOT)
    server = http.server.ThreadingHTTPServer(('0.0.0.0', port), handler)
    print(f'ParticleEditor Final live on http://0.0.0.0:{port}/Experimental/ParticleEditor%20Final/', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
