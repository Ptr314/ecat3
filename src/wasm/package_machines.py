#!/usr/bin/env python3
"""
Package eCat3 machine assets into per-machine bundles for WASM deployment.

Each bundle is a simple binary archive: for each file, a null-terminated
relative path, 4-byte LE size, then raw content. An empty filename marks
the end of the archive.

Generates, inside <output_dir>:
  - machines.json: manifest for the JS frontend
  - bundles/<machine-id>.bundle: per-machine asset archive
  - bundles/data.bundle: shared data files (charmaps, keyboard maps from
    deploy/data/)
  - tapefiles.ini: the [TapeFiles] section of deploy/.ecat.ini, which the page
    writes into the ini of the emulator - it says how a tape file of every
    extension goes onto the tape, and the core reads it when recording too

The bundles sit in their own subdirectory to keep the deployment package
readable: everything the browser loads first (page, module, manifest) stays
at the top level, and the couple of dozen machine archives do not bury it.
The manifest carries the subdirectory in the URLs, so the page fetches them
without knowing the layout.
"""

# Subdirectory of the output directory that the .bundle files go into.
# Referenced from machines.json, so changing it needs no change on the page.
BUNDLES_DIR = "bundles"

# The [TapeFiles] section of the desktop defaults, next to the page.
TAPE_FILES_NAME = "tapefiles.ini"

import os
import re
import sys
import json
import struct
import glob
import zipfile

def is_extension(path):
    """A configuration extension (.ext, or one packed as .ext.zip), see docs/CONFIG.md."""
    return path.lower().endswith(".ext") or path.lower().endswith(".ext.zip")

def machine_stem(path):
    """The file name without .cfg / .ext / .ext.zip."""
    name = os.path.basename(path)
    if name.lower().endswith(".ext.zip"):
        return name[:-8]
    return os.path.splitext(name)[0]

def find_cfg_files(computers_dir):
    """Find all machine files: .cfg and configuration extensions."""
    configs = []
    for root, dirs, files in os.walk(computers_dir):
        for f in files:
            if f.endswith(".cfg") or is_extension(f):
                configs.append(os.path.join(root, f))
    return sorted(configs)

def read_extension(ext_path):
    """
    Text of an extension. A packed one is the single .ext at the top of its
    archive; the archive goes into the bundle as it is and the emulator
    unpacks it, so its own files need no collecting here.
    """
    if not ext_path.lower().endswith(".zip"):
        with open(ext_path, "r", encoding="utf-8-sig") as f:
            return f.read()
    with zipfile.ZipFile(ext_path) as z:
        names = [n for n in z.namelist() if "/" not in n and n.lower().endswith(".ext")]
        if len(names) != 1:
            return ""
        return z.read(names[0]).decode("utf-8-sig")

def parse_ext_metadata(ext_path):
    """
    @extends, @version, the system properties the extension changes and the
    files its own lines refer to. Lines with inline data ({base64: ...}) refer
    to nothing: the data is in the file.
    """
    text = read_extension(ext_path)
    meta = {"extends": "", "version": "", "system": {}, "files": []}
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("@script"):
            break
        m = re.match(r'@(extends|version)\s+(.+)', s)
        if m:
            value = re.sub(r'\s+//.*$', '', m.group(2)).strip().strip('"')
            meta[m.group(1)] = value
            continue
        m = re.match(r'system\s*:\s*(\w+)\s*=\s*(.+)', s)
        if m:
            meta["system"][m.group(1)] = m.group(2).strip()
            continue
        m = re.match(r'[^-/@][^:]*:\s*(?:image|map|keys|picture)\s*=\s*([^\s{]+)', s)
        if m and "base64" not in s:
            meta["files"].append(m.group(1).strip('"'))
    return meta

def parse_cfg_metadata(cfg_path):
    """Extract system name, type, and referenced files from a .cfg."""
    with open(cfg_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Extract system block metadata
    name = ""
    version = ""
    sys_type = ""
    charmap = ""
    debug = False
    order = 0

    system_match = re.search(r'system\s*\{([^}]*)\}', content, re.DOTALL)
    if system_match:
        block = system_match.group(1)
        m = re.search(r'name\s*=\s*(.+)', block)
        if m: name = m.group(1).strip()
        m = re.search(r'version\s*=\s*(.+)', block)
        if m: version = m.group(1).strip()
        m = re.search(r'type\s*=\s*(.+)', block)
        if m: sys_type = m.group(1).strip()
        m = re.search(r'charmap\s*=\s*(.+)', block)
        if m: charmap = m.group(1).strip()
        m = re.search(r'debug\s*=\s*(.+)', block)
        if m: debug = m.group(1).strip() == "1"
        m = re.search(r'order\s*=\s*(-?\d+)', block)
        if m: order = int(m.group(1))

    # Files a config refers to: ROM/disk images, the host keyboard map, and the
    # native key table with the keyboard drawing the on-screen keyboard needs.
    # The line must start with the parameter, or "charmap" would match too.
    # A "map" may also name a device of the same machine - the page table of
    # an Argo memory controller is a ROM device, map = memcfg-rom - and such a
    # value is not a file to pack
    devices = set(re.findall(r'^\s*([\w-]+)\s*:\s*[\w-]+\s*\{', content, re.MULTILINE))
    files = []
    for m in re.finditer(r'^\s*(?:image|map|keys|picture)\s*=\s*(\S+)', content, re.MULTILINE):
        if m.group(1) not in devices:
            files.append(m.group(1))

    return {
        "name": name,
        "version": version,
        "type": sys_type,
        "charmap": charmap,
        "debug": debug,
        "order": order,
        "files": files,
        "devices": devices,
    }

def find_file(filename, search_dirs):
    """Search for a file in multiple directories and their subdirectories."""
    for d in search_dirs:
        path = os.path.join(d, filename)
        if os.path.isfile(path):
            return path
        # Also check files/ subdirectory
        path = os.path.join(d, "files", filename)
        if os.path.isfile(path):
            return path
        # Search one level of subdirectories (matches emulator's find_file_location behavior)
        if os.path.isdir(d):
            for sub in os.listdir(d):
                subpath = os.path.join(d, sub, filename)
                if os.path.isfile(subpath):
                    return subpath
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

def write_tape_files(deploy_dir, output_dir):
    """
    Copy the [TapeFiles] section of deploy/.ecat.ini, the desktop defaults, so
    the page and the desktop describe tape files from one place.
    """
    source = os.path.join(deploy_dir, ".ecat.ini")
    if not os.path.isfile(source):
        print(f"  WARNING: {source} not found, tapes will not load in the browser")
        return

    lines = []
    inside = False
    with open(source, "r", encoding="utf-8-sig") as f:
        for line in f:
            s = line.strip()
            if s.startswith("[") and s.endswith("]"):
                inside = s.lower() == "[tapefiles]"
                if inside:
                    lines.append(s)
                continue
            if inside and s and not s.startswith(";"):
                lines.append(s)

    target = os.path.join(output_dir, TAPE_FILES_NAME)
    with open(target, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Wrote {target} ({max(len(lines) - 1, 0)} tape formats)")

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <deploy_dir> <output_dir>")
        sys.exit(1)

    deploy_dir = sys.argv[1]
    output_dir = sys.argv[2]

    computers_dir = os.path.join(deploy_dir, "computers")
    data_dir = os.path.join(deploy_dir, "data")
    software_dir = os.path.join(deploy_dir, "software")

    bundles_dir = os.path.join(output_dir, BUNDLES_DIR)
    os.makedirs(bundles_dir, exist_ok=True)

    # Package shared data files
    data_files = {}
    if os.path.isdir(data_dir):
        for f in os.listdir(data_dir):
            fpath = os.path.join(data_dir, f)
            if os.path.isfile(fpath):
                data_files[f"data/{f}"] = fpath

    if data_files:
        data_bundle = os.path.join(bundles_dir, "data.bundle")
        create_bundle(data_files, data_bundle)
        print(f"Created {data_bundle} ({len(data_files)} files)")

    write_tape_files(deploy_dir, output_dir)

    # Process each machine config
    machines = []
    cfg_files = find_cfg_files(computers_dir)

    for machine_path in cfg_files:
        # An extension is packed together with its base: the bundle carries
        # both, and the emulator applies one to the other as on the desktop
        ext_path = None
        ext_meta = None
        if is_extension(machine_path):
            ext_path = machine_path
            ext_meta = parse_ext_metadata(ext_path)
            if not ext_meta["extends"] or not ext_meta["version"]:
                print(f"  WARNING: {os.path.basename(ext_path)}: no @extends or @version, skipped")
                continue
            cfg_path = os.path.normpath(os.path.join(computers_dir, ext_meta["extends"]))
            if not os.path.isfile(cfg_path) or not cfg_path.startswith(os.path.normpath(computers_dir)):
                print(f"  WARNING: {os.path.basename(ext_path)}: base {ext_meta['extends']} is not in computers/, skipped")
                continue
        else:
            cfg_path = machine_path

        meta = parse_cfg_metadata(cfg_path)
        if ext_meta is not None:
            meta["version"] = ext_meta["version"]
            system = ext_meta["system"]
            if "name" in system: meta["name"] = system["name"]
            if "type" in system: meta["type"] = system["type"]
            if "charmap" in system: meta["charmap"] = system["charmap"]
            if "debug" in system: meta["debug"] = system["debug"] == "1"
            if "order" in system and re.match(r'-?\d+$', system["order"]):
                meta["order"] = int(system["order"])
        if not meta["name"]:
            continue

        # Debug configurations are the test benches and the variants with a
        # diagnostic module plugged in. The desktop hides them behind an ini
        # setting; the page has no such setting, so they are not shipped at
        # all and their ROM images stay out of the download as well.
        if meta["debug"]:
            print(f"Skipped {os.path.basename(machine_path)} (debug configuration)")
            continue

        cfg_dir = os.path.dirname(cfg_path)
        cfg_filename = os.path.basename(cfg_path)
        machine_subdir = os.path.relpath(cfg_dir, deploy_dir).replace("\\", "/")  # e.g. "computers/agat"

        # Machine ID from the name of the file chosen (without extension)
        machine_id = machine_stem(machine_path)

        # Collect files for this machine's bundle
        bundle_files = {}

        # Include the .cfg file itself
        archive_cfg_path = f"{machine_subdir}/{cfg_filename}"
        bundle_files[archive_cfg_path] = cfg_path

        # The description beside the configuration, the one the desktop shows
        # in its chooser: the page opens it from the info button
        md_path = os.path.splitext(cfg_path)[0] + ".md"
        if os.path.isfile(md_path):
            bundle_files[f"{machine_subdir}/{os.path.splitext(cfg_filename)[0]}.md"] = md_path

        # Search directories for referenced files
        search_dirs = [cfg_dir, data_dir, software_dir]
        if os.path.isdir(os.path.join(cfg_dir, "files")):
            search_dirs.insert(1, os.path.join(cfg_dir, "files"))

        missing = []
        for ref_file in meta["files"]:
            local_path = find_file(ref_file, search_dirs)
            if local_path:
                # Determine archive path
                # Files from cfg_dir keep their relative position
                # Files from software/ or data/ are placed alongside the .cfg
                # so the emulator's find_file_location (which checks system_path first) finds them
                if local_path.startswith(cfg_dir):
                    rel = os.path.relpath(local_path, deploy_dir)
                elif local_path.startswith(data_dir):
                    rel = "data/" + os.path.relpath(local_path, data_dir)
                elif local_path.startswith(software_dir):
                    # Place alongside .cfg so emulator finds it via system_path
                    rel = machine_subdir + "/" + os.path.basename(local_path)
                else:
                    rel = f"{machine_subdir}/{ref_file}"
                bundle_files[rel.replace("\\", "/")] = local_path
            else:
                missing.append(ref_file)

        # The extension itself, its description and the files it names. They
        # keep their place relative to computers/: the emulator looks for the
        # files of an extension next to it first
        archive_machine_path = archive_cfg_path
        md_vfs_path = None
        if ext_path is not None:
            ext_dir = os.path.dirname(ext_path)
            ext_subdir = os.path.relpath(ext_dir, deploy_dir).replace("\\", "/")
            archive_machine_path = f"{ext_subdir}/{os.path.basename(ext_path)}"
            bundle_files[archive_machine_path] = ext_path
            ext_md = os.path.join(ext_dir, machine_id + ".md")
            if os.path.isfile(ext_md):
                bundle_files[f"{ext_subdir}/{machine_id}.md"] = ext_md
                md_vfs_path = f"/{ext_subdir}/{machine_id}.md"
            elif os.path.isfile(md_path):
                md_vfs_path = f"/{machine_subdir}/{os.path.splitext(cfg_filename)[0]}.md"
            for ref_file in ext_meta["files"]:
                # The devices are the base's: an extension names them too
                if ref_file in meta["devices"]:
                    continue
                local_path = find_file(ref_file, [ext_dir])
                if local_path:
                    rel = os.path.relpath(local_path, deploy_dir)
                    bundle_files[rel.replace("\\", "/")] = local_path
                    continue
                local_path = find_file(ref_file, search_dirs)
                if local_path:
                    bundle_files[f"{machine_subdir}/{os.path.basename(local_path)}"] = local_path
                else:
                    missing.append(ref_file)

        if missing:
            print(f"  WARNING: {machine_id}: missing files: {', '.join(missing)}")

        # Create bundle
        bundle_name = f"{machine_id}.bundle"
        bundle_path = os.path.join(bundles_dir, bundle_name)
        create_bundle(bundle_files, bundle_path)

        bundle_size = os.path.getsize(bundle_path)
        print(f"Created {bundle_name} ({len(bundle_files)} files, {bundle_size} bytes) - {meta['name']}")

        # Virtual FS path of the file the emulator loads: the .cfg, or the
        # extension, which finds its base in the same bundle
        cfg_vfs_path = f"/{archive_machine_path}"

        display_name = meta["version"] if meta["version"] else meta["name"]

        entry = {
            "id": machine_id,
            "name": display_name,
            "type": meta["type"],
            "family": meta["name"],
            "order": meta["order"],
            "cfg_path": cfg_vfs_path,
            "bundle_url": f"{BUNDLES_DIR}/{bundle_name}",
            "data_bundle_url": f"{BUNDLES_DIR}/data.bundle" if meta["charmap"] else None,
        }
        # An extension without a description of its own shows that of its base
        if md_vfs_path:
            entry["md_path"] = md_vfs_path
        machines.append(entry)

    # The arrangement of the desktop chooser (OpenConfigWindow::list_machines):
    # machines are grouped by type under the name of the first config of that
    # type, the families follow in the order of that name, and inside a family
    # the machines go by "order", then by their own name. The page shows the
    # manifest as it is, so the order is settled here once.
    family_names = {}
    for m in machines:
        family_names.setdefault(m["type"], m["family"])
    for m in machines:
        m["family"] = family_names[m["type"]]

    def text_key(s):
        return (s.casefold(), s)

    machines.sort(key=lambda m: (text_key(m["family"]), m["type"], m["order"], text_key(m["name"])))

    # Write manifest
    manifest_path = os.path.join(output_dir, "machines.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(machines, f, indent=2, ensure_ascii=False)
    print(f"\nWrote {manifest_path} with {len(machines)} machines")

if __name__ == "__main__":
    main()
