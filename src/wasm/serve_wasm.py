#!/usr/bin/env python3
"""
Simple HTTP server with COOP/COEP headers required for SharedArrayBuffer.
Usage: python3 serve_wasm.py [directory] [port]
"""

import http.server
import sys
import os

class COOPCOEPHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

    os.chdir(directory)
    server = http.server.HTTPServer(("", port), COOPCOEPHandler)
    print(f"Serving {os.path.abspath(directory)} on http://localhost:{port}")
    print("(COOP/COEP headers enabled for SharedArrayBuffer)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
