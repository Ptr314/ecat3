#!/usr/bin/env python3
"""
Stamp a browser package with the versions of its files.

Usage: stamp_release.py <release_dir>

Every file of the package keeps its name from build to build, so a browser
that has one in its cache may run an old ecat3.wasm with a new ecat3.js, or an
old .bundle with a new machines.json. This step, the last one of build-wasm,
writes manifest.json next to the page:

  {"version": "4.4.0", "build": "<hash>", "files": {"ecat3.wasm": "<hash>", ...}}

with the hash of the content of every file, and stamps index.html: the two
scripts get "?h=<hash>" in their addresses and the page carries the build in
<meta name="ecat-build">. ecat_wasm.js adds the hash of the manifest to every
file it fetches, so an address changes exactly when its content does, and it
reads the manifest past the cache at every start: a build there different from
the one of the page means the page itself came out of the cache, and it is
loaded again once, by a new address (see checkBuild() in ecat_wasm.js).

The step can be repeated on a stamped package (after copying a new
ecat_wasm.js or shell.html into it by hand): the stamps of index.html are
replaced, not added to.
"""

import hashlib
import json
import os
import re
import sys

MANIFEST_NAME = "manifest.json"
PAGE_NAME = "index.html"
HASH_LENGTH = 12

# The scripts of the page and the build marker, stamped or not
SCRIPT_SRC = re.compile(r'(<script\b[^>]*\bsrc=")([^"?]+)(?:\?h=[0-9a-f]*)?(")')
BUILD_META = re.compile(r'(<meta name="ecat-build" content=")[^"]*(")')


def file_hash(data):
    return hashlib.sha256(data).hexdigest()[:HASH_LENGTH]


def read_version():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    with open(os.path.join(root, "VERSION"), "r", encoding="utf-8") as f:
        return f.read().strip()


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        print("Usage: stamp_release.py <release_dir>", file=sys.stderr)
        return 1
    release_dir = os.path.abspath(sys.argv[1])
    page_path = os.path.join(release_dir, PAGE_NAME)

    with open(page_path, "r", encoding="utf-8") as f:
        page = f.read()
    if not BUILD_META.search(page):
        print(f"{page_path}: no <meta name=\"ecat-build\">", file=sys.stderr)
        return 1
    # The page without its stamps, so a repeated run hashes the same text
    page = SCRIPT_SRC.sub(r"\1\2\3", page)
    page = BUILD_META.sub(r"\1\2", page)

    files = {}
    for root, dirs, names in os.walk(release_dir):
        dirs.sort()
        for name in sorted(names):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, release_dir).replace("\\", "/")
            if rel in (PAGE_NAME, MANIFEST_NAME):
                continue
            with open(path, "rb") as f:
                files[rel] = file_hash(f.read())

    # The build changes with any file of the package, the page included
    summary = "\n".join(f"{k} {v}" for k, v in sorted(files.items()))
    build = file_hash((file_hash(page.encode("utf-8")) + "\n" + summary).encode("utf-8"))

    def stamp_script(m):
        h = files.get(m.group(2))
        return m.group(1) + m.group(2) + ("?h=" + h if h else "") + m.group(3)

    page = SCRIPT_SRC.sub(stamp_script, page)
    page = BUILD_META.sub(lambda m: m.group(1) + build + m.group(2), page)
    with open(page_path, "w", encoding="utf-8", newline="") as f:
        f.write(page)

    manifest = {"version": read_version(), "build": build, "files": files}
    with open(os.path.join(release_dir, MANIFEST_NAME), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    print(f"Stamped {PAGE_NAME}: build {build}, {len(files)} files in {MANIFEST_NAME}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
