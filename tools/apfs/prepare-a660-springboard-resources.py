#!/usr/bin/env python3
"""Restore the SpringBoard Metal resources in a private 24B119 RAM-boot package."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import runpy
import shutil
import stat
from tempfile import TemporaryDirectory

import apfs
import mkapfs


BUILD = "24B119"
SYSTEM_VERSION = "/System/Library/CoreServices/SystemVersion.plist"
VOLUMES = (("System", 0x1), ("Preboot", 0x10))
MAX_USB_IMAGE = 0xFFFFFFFF


class RepackedVolume(mkapfs.Volume):
    def is_plain(self, node):
        return node.keep_plain


def volume(container, name, role):
    selected = apfs.pick_volume(container, name)
    if selected.role != role:
        raise ValueError(f"{name}: unexpected APFS volume role {selected.role:#x}")
    return selected


def read_file(selected, path):
    hit = selected.lookup(path)
    if hit is None:
        raise ValueError(f"{selected.volname}: missing {path}")
    inode = selected.inode(hit[0])
    if not stat.S_ISREG(inode.mode):
        raise ValueError(f"{selected.volname}: {path} is not a regular file")
    return inode, inode.contents()


def build_id(selected):
    _, plist = read_file(selected, SYSTEM_VERSION)
    return plistlib.loads(plist).get("ProductBuildVersion")


def signature(inode, content):
    return inode.mode, inode.owner, inode.group, hashlib.sha256(content).hexdigest()


def snapshot(selected, staging=None):
    root = selected.lookup("/")[0]
    nodes = {}
    records = {}
    entries = [("/", "", root, 4)]
    entries.extend(apfs.walk(selected, root))
    kept_xattrs = {apfs.DECMPFS_XATTR_NAME, apfs.RSRC_FORK_XATTR_NAME, apfs.SYMLINK_EA_NAME}
    for path, name, fid, _ in entries:
        inode = selected.inode(fid)
        if set(inode.xattrs) - kept_xattrs:
            raise ValueError(f"{selected.volname}: unexpected extended attributes on {path}")
        source = target = None
        if stat.S_ISREG(inode.mode):
            content = inode.contents()
            if staging is not None:
                source = staging / f"{fid:x}"
                if not source.exists():
                    source.write_bytes(content)
        elif stat.S_ISLNK(inode.mode):
            target = content = inode.symlink_target_bytes()
            if target is None:
                raise ValueError(f"{selected.volname}: missing link target for {path}")
        elif stat.S_ISDIR(inode.mode):
            content = b""
        else:
            raise ValueError(f"{selected.volname}: unsupported file type at {path}")
        records[path] = signature(inode, content)
        if staging is not None:
            node = mkapfs.Node(name, inode.mode, source=source, target=target)
            node.uid, node.gid = inode.owner, inode.group
            node.keep_plain = inode.decmpfs is None
            nodes[path] = node
            if path != "/":
                parent = path.rpartition("/")[0] or "/"
                nodes[parent].children[name] = node
    return records, nodes


def build_system(base, donor, out, assets):
    previous = apfs.Container(base)
    original = apfs.Container(donor)
    expected = {}
    try:
        source = apfs.pick_volume(original, None)
        if build_id(source) != BUILD or build_id(volume(previous, *VOLUMES[0])) != BUILD:
            raise ValueError(f"Both system images must have ProductBuildVersion {BUILD}")
        libraries = {}
        for path in assets:
            inode, data = read_file(source, path)
            library = data[:4] in (b"MTLB", bytes.fromhex("cafebabe"))
            archive = path.endswith("/archive.metallib") and data[:8] == bytes.fromhex("cffaedfe13000001")
            if not library and not archive:
                raise ValueError(f"{path}: unexpected Metal library signature")
            libraries[path] = (inode, data)
        with TemporaryDirectory(prefix="metal-system-", dir=out.parent) as work:
            scratch = Path(work)
            stage = []
            for name, role in VOLUMES:
                staging = scratch / name
                staging.mkdir()
                records, nodes = snapshot(volume(previous, name, role), staging)
                expected[name] = records
                stage.append((name, role, nodes["/"]))
                print(f"Read {len(records)} entries from {name}", flush=True)
                if name == "System":
                    for index, path in enumerate(assets):
                        inode, data = libraries[path]
                        if path in records:
                            if records[path][3] != hashlib.sha256(data).hexdigest():
                                raise ValueError(f"{path}: the base contains different bytes")
                            continue
                        parent, _, filename = path.rpartition("/")
                        staged = staging / f"metal-{index}"
                        staged.write_bytes(data)
                        node = mkapfs.Node(filename, inode.mode, source=staged)
                        node.keep_plain = False
                        node.uid, node.gid = inode.owner, inode.group
                        nodes[parent].children[filename] = node
                        records[path] = signature(inode, data)
                        print(f"Restored {path} ({len(data)} bytes)", flush=True)

            partial = scratch / "system.apfs"
            with (scratch / "compression").open("w+b") as spool:
                volumes = [RepackedVolume(i, name, root, True, 1683900000 * 10**9, spool, role=role)
                           for i, (name, role, root) in enumerate(stage)]
                rebuilt = mkapfs.Container(volumes, 256, out.name)
                rebuilt.layout()
                if rebuilt.block_count * mkapfs.BS > MAX_USB_IMAGE:
                    raise ValueError("Restored image exceeds the fastboot download size limit")
                rebuilt.write(partial)
            print(f"Readback of {partial.stat().st_size} bytes", flush=True)
            checked = apfs.Container(partial)
            try:
                for name, role in VOLUMES:
                    records, _ = snapshot(volume(checked, name, role))
                    if records != expected[name]:
                        different = sorted(path for path in records.keys() | expected[name].keys()
                                           if records.get(path) != expected[name].get(path))
                        raise ValueError(f"{name}: changed files or metadata: {different[:12]}")
            finally:
                checked.close()
            os.replace(partial, out)
    finally:
        previous.close()
        original.close()


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--base", type=Path, required=True)
    cli.add_argument("--donor", type=Path, required=True)
    cli.add_argument("--output", type=Path, required=True)
    args = cli.parse_args()
    base, out = args.base.resolve(), args.output.resolve()
    meta = json.loads((base / "manifest.json").read_text(encoding="utf-8"))
    required = {"gpu=1", "metal=1", "wifi=0", "bt=0", "ufs=0", "ufs_write=0", "var_store=0",
                "metal_procs=SpringBoard"}
    if not required.issubset(meta["boot_arguments"]) or "metal_all=1" in meta["boot_arguments"]:
        raise ValueError("Expected the offline SpringBoard Metal profile")
    repo = Path(__file__).resolve().parents[2]
    selection = runpy.run_path(str(repo / "darwin/tools/mkimage26.py"))
    assets = ["/" + path for path in selection["METAL_FILES"]]
    out.mkdir()
    target = out / "system.apfs"
    build_system(base / "system.apfs", args.donor, target, assets)
    with target.open("rb") as raw:
        checksum = hashlib.file_digest(raw, "sha256").hexdigest()
    meta["files"]["system.apfs"] = {"bytes": target.stat().st_size, "sha256": checksum}
    meta["stage"] = "lemonade-a660-springboard-metal-resources"
    for flag in ("ios_started_verified", "springboard_verified", "hardware_boot_verified", "gpu_metal_verified"):
        meta[flag] = False
    for name in ("boot.img", "kernel", "root.apfs"):
        os.link(base / name, out / name)
    for name in ("launch.py", "mkpayload.py", "Start.ps1", "Return-to-Android.ps1"):
        shutil.copyfile(base / name, out / name)
    start_script = base / "Start-iOS.cmd"
    if start_script.is_file():
        shutil.copyfile(start_script, out / start_script.name)
    (out / "manifest.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    names = ("boot.img", "kernel", "root.apfs", "system.apfs", "mkpayload.py")
    (out / "SHA256SUMS").write_text("".join(f"{meta['files'][n]['sha256']}  {n}\n" for n in names),
                                   encoding="ascii")
    print(f"Verified both volumes and the {BUILD} Metal resources at {out}", flush=True)


if __name__ == "__main__":
    main()
