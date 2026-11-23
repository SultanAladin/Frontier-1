# SERVEPARTICLEEDITOR.PY
# 🧩 Static server for the standalone particle editor: serves this folder only, so "/" opens the editor directly.
#    Nothing outside Experimental/ParticleEditor is reachable or needed.
# 📝 Usage: python3 ServeParticleEditor.py [port]   (default 8097, binds 0.0.0.0). Any static server rooted here also works.

import http.server
import os
import sys

AppRoot = os.path.dirname(os.path.abspath(__file__))


class EditorRequest(http.server.SimpleHTTPRequestHandler):
    extensions_map = { **http.server.SimpleHTTPRequestHandler.extensions_map,
                       '.js': 'text/javascript', '.woff2': 'font/woff2', '.facet': 'application/octet-stream' }

    def __init__(self, *Arguments, **Options):
        super().__init__(*Arguments, directory=AppRoot, **Options)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


if __name__ == '__main__':
    Port = int(sys.argv[1]) if len(sys.argv) > 1 else 8097
    print(f'Particle editor on http://0.0.0.0:{Port}/', flush=True)
    http.server.ThreadingHTTPServer(('0.0.0.0', Port), EditorRequest).serve_forever()
