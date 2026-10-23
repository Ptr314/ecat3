#!/usr/bin/env python3
"""
Local HTTP server for the browser build, with the COOP/COEP headers
SharedArrayBuffer needs (the emulator is built with threads).

Usage: python3 serve_wasm.py [directory] [port] [--open] [--cache]

For trying a build, not for deployment: every answer carries
Cache-Control: no-store, so a reload after a rebuild never runs the old
ecat3.wasm out of the browser cache.

--cache does the opposite, to try the versions of the files (checkBuild() in
ecat_wasm.js): an address with ?h=<hash> is cached for good, everything else,
the page included, for an hour - a server set up carelessly. A page restamped
under it has to come back new by itself.
"""

import argparse
import http.server
import os
import sys
import urllib.parse
import webbrowser


class COOPCOEPHandler(http.server.SimpleHTTPRequestHandler):
    # The Windows registry may map .js to text/plain, which a browser refuses
    # to run, and older Pythons do not know .wasm at all (no streaming compile
    # without application/wasm).
    extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map)
    extensions_map.update({
        ".js": "text/javascript",
        ".wasm": "application/wasm",
        ".json": "application/json",
        ".html": "text/html",
    })

    cache = False

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        if not self.cache:
            self.send_header("Cache-Control", "no-store")
        elif "h" in urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query):
            self.send_header("Cache-Control", "public, max-age=31536000, immutable")
        else:
            self.send_header("Cache-Control", "public, max-age=3600")
        super().end_headers()


class Server(http.server.ThreadingHTTPServer):
    # On Windows SO_REUSEADDR lets a second server bind a port that is already
    # taken, and requests then go to either of them. Fail instead.
    allow_reuse_address = os.name != "nt"
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("directory", nargs="?", default=".")
    parser.add_argument("port", nargs="?", type=int, default=8080)
    parser.add_argument("--open", action="store_true",
                        help="open the page in the default browser")
    parser.add_argument("--cache", action="store_true",
                        help="let the browser cache the files, see above")
    args = parser.parse_args()
    COOPCOEPHandler.cache = args.cache

    directory = os.path.abspath(args.directory)
    if not os.path.isfile(os.path.join(directory, "index.html")):
        print(f"No index.html in {directory}", file=sys.stderr)
        return 1
    os.chdir(directory)

    try:
        server = Server(("", args.port), COOPCOEPHandler)
    except OSError as e:
        print(f"Cannot listen on port {args.port}: {e}", file=sys.stderr)
        return 1

    url = f"http://localhost:{args.port}/"
    print(f"Serving {directory} on {url}")
    print("(COOP/COEP headers enabled for SharedArrayBuffer)")
    # The socket is already listening, so the browser's first request waits
    # in the backlog until serve_forever() picks it up.
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
