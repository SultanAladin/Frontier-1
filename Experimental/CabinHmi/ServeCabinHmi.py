# SERVECABINHMI.PY
# 🧩 Static server for the cabin HMI prototypes: serves the repository root (the HMIs import shared modules and
#    facet models from Experimental/Shared) and sends "/" to the launcher page.
# 📝 Usage: python3 Experimental/CabinHmi/ServeCabinHmi.py [port]   (default 8097, binds 0.0.0.0)

import http.server
import os
import sys

RepositoryRoot = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
Launcher       = '/Experimental/CabinHmi/'


class CabinRequest(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *Arguments, **Options):
        super().__init__(*Arguments, directory=RepositoryRoot, **Options)

    def do_GET(self):
        if self.path in ('/', '/index.html'):
            self.send_response(302)
            self.send_header('Location', Launcher)
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


if __name__ == '__main__':
    Port = int(sys.argv[1]) if len(sys.argv) > 1 else 8097
    print(f'Cabin HMI on http://0.0.0.0:{Port}{Launcher}', flush=True)
    http.server.ThreadingHTTPServer(('0.0.0.0', Port), CabinRequest).serve_forever()
