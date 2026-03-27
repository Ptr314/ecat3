#!/usr/bin/env python3
"""
Package eCat3 machine assets into per-machine bundles for WASM deployment.

Each bundle is a simple binary archive: for each file, a null-terminated
relative path, 4-byte LE size, then raw content. An empty filename marks
the end of the archive.

Generates:
  - machines.json: manifest for the JS frontend
  - <machine-id>.bundle: per-machine asset archive
  - data.bundle: shared data files (charmaps, keyboard maps from deploy/data/)
"""

import os
import re
import sys
import json
import struct
import glob

def find_cfg_files(computers_dir):
    """Find all .cfg files, skipping test configs."""
    configs = []
    for root, dirs, files in os.walk(computers_dir):
        for f in files:
            if f.endswith(".cfg"):
                # Skip test configs
                if "test" in f.lower() or "zexall" in f.lower():
                    continue
                configs.append(os.path.join(root, f))
    return sorted(configs)

def parse_cfg_metadata(cfg_path):
    """Extract system name, type, and referenced files from a .cfg."""
    with open(cfg_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Extract system block metadata
    name = ""
    sys_type = ""
    charmap = ""

    system_match = re.search(r'system\s*\{([^}]*)\}', content, re.DOTALL)
    if system_match:
        block = system_match.group(1)
        m = re.search(r'name\s*=\s*(.+)', block)
        if m: name = m.group(1).strip()
        m = re.search(r'type\s*=\s*(.+)', block)
        if m: sys_type = m.group(1).strip()
        m = re.search(r'charmap\s*=\s*(.+)', block)
        if m: charmap = m.group(1).strip()

    # Find all image = and map = references
    files = []
    for m in re.finditer(r'(?:image|map)\s*=\s*(\S+)', content):
        files.append(m.group(1))

    return {
        "name": name,
        "type": sys_type,
        "charmap": charmap,
        "files": files,
    }

def find_file(filename, search_dirs):
    """Search for a file in multiple directories."""
    for d in search_dirs:
        path = os.path.join(d, filename)
        if os.path.isfile(path):
            return path
        # Also check files/ subdirectory
        path = os.path.join(d, "files", filename)
        if os.path.isfile(path):
            return path
    return None

def create_bundle(files_map, output_path):
    """
    Create a binary bundle from a dict of {archive_path: local_path}.
    Format: for each file: null-terminated path + 4-byte LE size + content.
    Ends with an empty name (single null byte).
    """
    with open(output_path, "wb") as out:
        for archive_path, local_path in sorted(files_map.items()):
            with open(local_path, "rb") as f:
                data = f.read()
            # Write: path (null-terminated) + size (4 bytes LE) + data
            out.write(archive_path.encode("utf-8") + b"\x00")
            out.write(struct.pack("<I", len(data)))
            out.write(data)
        # End marker: empty name
        out.write(b"\x00")

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <deploy_dir> <output_dir>")
        sys.exit(1)

    deploy_dir = sys.argv[1]
    output_dir = sys.argv[2]

    computers_dir = os.path.join(deploy_dir, "computers")
    data_dir = os.path.join(deploy_dir, "data")
    software_dir = os.path.join(deploy_dir, "software")

    os.makedirs(output_dir, exist_ok=True)

    # Package shared data files
    data_files = {}
    if os.path.isdir(data_dir):
        for f in os.listdir(data_dir):
            fpath = os.path.join(data_dir, f)
            if os.path.isfile(fpath):
                data_files[f"data/{f}"] = fpath

    if data_files:
        data_bundle = os.path.join(output_dir, "data.bundle")
        create_bundle(data_files, data_bundle)
        print(f"Created {data_bundle} ({len(data_files)} files)")

    # Process each machine config
    machines = []
    cfg_files = find_cfg_files(computers_dir)

    for cfg_path in cfg_files:
        meta = parse_cfg_metadata(cfg_path)
        if not meta["name"]:
            continue

        cfg_dir = os.path.dirname(cfg_path)
        cfg_filename = os.path.basename(cfg_path)
        machine_subdir = os.path.relpath(cfg_dir, deploy_dir)  # e.g. "computers/agat"

        # Machine ID from config filename (without extension)
        machine_id = os.path.splitext(cfg_filename)[0]

        # Collect files for this machine's bundle
        bundle_files = {}

        # Include the .cfg file itself
        archive_cfg_path = f"{machine_subdir}/{cfg_filename}"
        bundle_files[archive_cfg_path] = cfg_path

        # Search directories for referenced files
        search_dirs = [cfg_dir, data_dir, software_dir]
        if os.path.isdir(os.path.join(cfg_dir, "files")):
            search_dirs.insert(1, os.path.join(cfg_dir, "files"))

        missing = []
        for ref_file in meta["files"]:
            local_path = find_file(ref_file, search_dirs)
            if local_path:
                # Determine archive path relative to the search location
                # Files found in cfg_dir go under machine_subdir
                if local_path.startswith(cfg_dir):
                    rel = os.path.relpath(local_path, deploy_dir)
                elif local_path.startswith(data_dir):
                    rel = "data/" + os.path.relpath(local_path, data_dir)
                elif local_path.startswith(software_dir):
                    rel = "software/" + os.path.relpath(local_path, software_dir)
                else:
                    rel = f"{machine_subdir}/{ref_file}"
                bundle_files[rel.replace("\\", "/")] = local_path
            else:
                missing.append(ref_file)

        if missing:
            print(f"  WARNING: {machine_id}: missing files: {', '.join(missing)}")

        # Create bundle
        bundle_name = f"{machine_id}.bundle"
        bundle_path = os.path.join(output_dir, bundle_name)
        create_bundle(bundle_files, bundle_path)

        bundle_size = os.path.getsize(bundle_path)
        print(f"Created {bundle_name} ({len(bundle_files)} files, {bundle_size} bytes) - {meta['name']}")

        # Virtual FS path for the .cfg
        cfg_vfs_path = f"/{archive_cfg_path}"

        machines.append({
            "id": machine_id,
            "name": meta["name"],
            "type": meta["type"],
            "cfg_path": cfg_vfs_path,
            "bundle_url": bundle_name,
            "data_bundle_url": "data.bundle" if meta["charmap"] else None,
        })

    # Write manifest
    manifest_path = os.path.join(output_dir, "machines.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(machines, f, indent=2, ensure_ascii=False)
    print(f"\nWrote {manifest_path} with {len(machines)} machines")

if __name__ == "__main__":
    main()
