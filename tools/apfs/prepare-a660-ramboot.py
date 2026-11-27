"""Prepare an offline OnePlus 9 RAM-boot test package on the development machine."""
from argparse import ArgumentParser
import hashlib
import json
import os
import plistlib
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory

import apfs

repo = Path(__file__).resolve().parents[2]

firmware_names = ("a660_gmu.bin", "a660_sqe.fw", "a660_zap.mdt",
                  "a660_zap.b00", "a660_zap.b01", "a660_zap.b02")
probe_name = "hakios-metal-probe"
rc_path = "etc/rc"
compression_attrs = {"com.apple.decmpfs", "com.apple.ResourceFork"}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def snapshot(image, stage=None):
    container = apfs.Container(str(image))
    records = {}
    manifest = []
    try:
        volume = apfs.pick_volume(container, "hakios")
        for path, _, oid, _ in apfs.walk(volume, apfs.ROOT_DIR_INO_NUM):
            name = path.lstrip("/")
            relative = PurePosixPath(name)
            if not relative.parts or any(p in {".", ".."} or any(c in p for c in "\\:\t\r\n")
                                         for p in relative.parts):
                raise ValueError(f"Invalid RAM root path {name!r}")
            inode = volume.inode(oid)
            target = stage.joinpath(*relative.parts) if stage is not None else None
            data = b""
            attrs = set(inode.xattrs)
            if inode.is_dir:
                if target is not None:
                    target.mkdir()
                extra = attrs
            elif inode.is_reg:
                data = inode.contents()
                if target is not None:
                    target.write_bytes(data)
                extra = attrs - compression_attrs if inode.decmpfs is not None else attrs
            elif inode.is_link:
                data = inode.symlink_target_bytes()
                if not data or any(ch in data for ch in b"\t\r\n"):
                    raise ValueError(f"Invalid RAM root symlink {name!r}")
                extra = attrs - {apfs.SYMLINK_EA_NAME}
            else:
                raise ValueError(f"Unsupported inode for {name!r}")
            if extra:
                raise ValueError(f"Unexpected xattrs in {name!r}: {sorted(extra)}")
            values = (inode.mode, inode.owner, inode.group)
            records[name] = (values, sha(data))
            if stage is not None:
                fields = f"{values[0]:o}\t{values[1]}\t{values[2]}\t{name}".encode()
                manifest.append(fields + (b"\t" + data if inode.is_link else b""))
    finally:
        container.close()
    return records, manifest

def append_manifest(entries, name, mode, data, values=None):
    values = values or (mode, 0, 0)
    entries[name] = (values, sha(data))
    return f"{values[0]:o}\t{values[1]}\t{values[2]}\t{name}".encode()

def verify_macho(data):
    if len(data) < 32:
        raise ValueError("Truncated probe")
    magic, cpu, subtype, _, count, size = struct.unpack_from("<IIIIII", data)
    if magic != 0xfeedfacf or cpu != 0x0100000c or subtype & 0xffffff != 2 or 32 + size > len(data):
        raise ValueError("Probe must be an arm64e Mach-O")
    end = 32 + size
    cursor = 32
    has_chained_fixups = False
    has_signature = False
    for _ in range(count):
        if end - cursor < 8:
            break
        kind, length = struct.unpack_from("<II", data, cursor)
        if length < 8 or cursor + length > end:
            break
        if kind in (0x1d, 0x80000034) and length >= 16:
            offset, payload_size = struct.unpack_from("<II", data, cursor + 8)
            if offset < end or payload_size == 0 or offset + payload_size > len(data):
                raise ValueError("Invalid probe Mach-O fixups or signature range")
            if kind == 0x1d:  # LC_CODE_SIGNATURE
                has_signature = True
            else:  # LC_DYLD_CHAINED_FIXUPS
                has_chained_fixups = True
        cursor += length
    if not has_signature or not has_chained_fixups:
        raise ValueError("Probe needs a Mach-O signature and chained fixups for Darwin 27.2")

def verify_firmware(image, firmware):
    container = apfs.Container(str(image))
    try:
        volume = apfs.pick_volume(container, "hakios")
        for name, data in firmware.items():
            hit = volume.lookup("fw-gpu/" + name)
            if hit is None:
                raise ValueError(f"Missing firmware after build: {name}")
            inode = volume.inode(hit[0])
            extents = volume.extents(inode.private_id)
            if inode.decmpfs is not None or len(extents) != 1 or not extents[0][2] or inode.contents() != data:
                raise ValueError(f"Firmware needs one plain extent: {name}")
    finally:
        container.close()

def build(base, firmware_dir, output, probe, probe_mode, bundle, trace_ui=False, host_clock=False):
    if trace_ui and bundle is None:
        raise ValueError("UI tracing requires the opt-in Metal bundle")
    metadata = json.loads((base / "manifest.json").read_text(encoding="utf-8"))
    args = metadata["boot_arguments"]
    if args.count("gpu=0") != 1 or not {"metal=0", "wifi=0", "ufs_write=0", "var_store=0"}.issubset(args):
        raise ValueError("Wrong base package or unsafe boot settings")
    if [arg for arg in args if arg.startswith("wifi=")] != ["wifi=0"] or any(
        arg.startswith("bt=") and arg != "bt=0" for arg in args
    ):
        raise ValueError("Wi-Fi and Bluetooth must be off")
    firmware = {name: (firmware_dir / name).read_bytes() for name in firmware_names}
    if not all(firmware.values()):
        raise ValueError("An A660 firmware image is empty")
    probe_data = probe.read_bytes() if probe else None
    if probe_data is not None:
        verify_macho(probe_data)
    bundle_files = {}
    if bundle is not None:
        bundle_files = {name: (bundle / name).read_bytes() for name in ("HakiosMetal", "Info.plist")}
        verify_macho(bundle_files["HakiosMetal"])
        if b"A660-COMPOSITOR opened" not in bundle_files["HakiosMetal"]:
            raise ValueError("The Metal bundle has no A660 compositor probe support")
        if plistlib.loads(bundle_files["Info.plist"]).get("CFBundleExecutable") != "HakiosMetal":
            raise ValueError("Unexpected Metal bundle executable")

    output.mkdir()
    with TemporaryDirectory(prefix="a660-root-", dir=output) as staging:
        work = Path(staging)
        stage = work / "tree"
        stage.mkdir()
        expected, manifest = snapshot(base / "root.apfs", stage)
        if any(path == "fw-gpu" or path.startswith("fw-gpu/") for path in expected):
            raise ValueError("Base RAM root already contains GPU firmware")
        (stage / "fw-gpu").mkdir()
        manifest.append(append_manifest(expected, "fw-gpu", 0o40755, b""))
        for name, data in firmware.items():
            file = "fw-gpu/" + name
            (stage / file).write_bytes(data)
            manifest.append(append_manifest(expected, file, 0o100644, data))

        if probe_data is not None:
            bin_path = "bin/" + probe_name
            if bin_path in expected:
                raise ValueError("Base RAM root already contains the A660 probe")
            (stage / bin_path).write_bytes(probe_data)
            manifest.append(append_manifest(expected, bin_path, 0o100755, probe_data))
            rc = stage / rc_path
            original_rc = rc.read_bytes()
            if b"exec /sbin/launchd.darwin" not in original_rc:
                raise ValueError("The 27.2 launchd handoff in /etc/rc has changed")
            flag = "--a660-smoke" if probe_mode == "memory" else f"--a660-{probe_mode}-smoke"
            content = f"/hakios/bin/hakios-metal-probe {flag}\n".encode() + original_rc
            rc.write_bytes(content)
            expected[rc_path] = (expected[rc_path][0], sha(content))

        if bundle_files:
            bundle_path = "lib/HakiosMetal.bundle"
            if bundle_path in expected:
                raise ValueError("Base RAM root already contains a Metal bundle")
            (stage / bundle_path).mkdir()
            manifest.append(append_manifest(expected, bundle_path, 0o40755, b""))
            for name, data in bundle_files.items():
                file = bundle_path + "/" + name
                mode = 0o100755 if name == "HakiosMetal" else 0o100644
                (stage / file).write_bytes(data)
                manifest.append(append_manifest(expected, file, mode, data))

        manifest_path = work / "root-manifest.tsv"
        manifest_path.write_bytes(b"\n".join(manifest) + b"\n")
        root_image = output / "root.apfs"
        subprocess.run([sys.executable, str(repo / "tools/apfs/mkapfs.py"), "-z",
                        "--plain", "fw-gpu", "-o", str(root_image),
                        f"hakios={stage},manifest={manifest_path}"], check=True)
        actual, _ = snapshot(root_image)
        if expected != actual:
            changed = sorted(name for name in set(expected) | set(actual) if expected.get(name) != actual.get(name))
            raise ValueError(f"Root metadata or contents changed: {changed}")
        verify_firmware(root_image, firmware)

    for name in ("boot.img", "kernel", "system.apfs"):
        os.link(base / name, output / name)
    for name in ("launch.py", "mkpayload.py", "Start.ps1", "Return-to-Android.ps1"):
        shutil.copyfile(base / name, output / name)
    start_script = base / "Start-iOS.cmd"
    if start_script.is_file():
        shutil.copyfile(start_script, output / start_script.name)
    if host_clock:
        launcher_path = output / "launch.py"
        launcher = launcher_path.read_text(encoding="utf-8")
        stage = "    with tempfile.TemporaryDirectory(prefix='lemonade-ramboot-') as temporary:"
        join = "' '.join(manifest['boot_arguments'])"
        if launcher.count(stage) != 1 or launcher.count(join) != 1:
            raise ValueError("Cannot locate payload creation in the boot launcher")
        # Resolve the epoch after the multi-gigabyte system image has crossed USB.
        read_time = """    boot_arguments = list(manifest['boot_arguments'])
    if not any(arg.startswith('epoch=') for arg in boot_arguments):
        host_epoch = int(time.time())
        boot_arguments.append(f'epoch={host_epoch}')
        print(f'Host UTC epoch for RAM boot: {host_epoch}', flush=True)
"""
        launcher = launcher.replace(stage, read_time + stage).replace(join, "' '.join(boot_arguments)")
        compile(launcher, str(launcher_path), "exec")
        launcher_path.write_text(launcher, encoding="utf-8", newline="\n")
    root_data = root_image.read_bytes()
    metadata["files"]["root.apfs"] = {"bytes": len(root_data), "sha256": sha(root_data)}
    if bundle_files:
        metadata["stage"] = "a660-compositor-trace" if trace_ui else "a660-compositor-probe"
    else:
        metadata["stage"] = f"a660-{probe_mode}-smoke" if probe is not None else "a660-firmware-preflight"
    overrides = {"gpu=0": "gpu=1"}
    if bundle_files:
        overrides["metal=0"] = "metal=1"
    metadata["boot_arguments"] = [overrides.get(arg, arg) for arg in args if not arg.startswith("bt=")]
    metadata["boot_arguments"].append("bt=0")
    if trace_ui:
        metadata["boot_arguments"] += ["metal_trace=1", "metal_fps=draws"]
    for key in ("ios_started_verified", "springboard_verified", "hardware_boot_verified", "gpu_metal_verified"):
        metadata[key] = False
    (output / "manifest.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    verify_names = ("boot.img", "kernel", "root.apfs", "system.apfs", "mkpayload.py")
    (output / "SHA256SUMS").write_text(
        "".join(f"{metadata['files'][name]['sha256']}  {name}\n" for name in verify_names), encoding="ascii")
    print(f"Verified {len(expected)} APFS paths and {len(firmware)} plain firmware files.")
    print(f"RAM root: {len(root_data)} bytes. Private package: {output}")

if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    binaries = parser.add_mutually_exclusive_group()
    binaries.add_argument("--probe", type=Path)
    binaries.add_argument("--metal-bundle", type=Path)
    parser.add_argument("--probe-mode", choices=("memory", "compute", "blit", "draw", "mask", "gmem", "gmem-binned"), default="memory")
    parser.add_argument("--trace-ui", action="store_true",
                        help="trace compositor draws and GPU timings")
    parser.add_argument("--host-clock", action="store_true",
                        help="set the offline wall clock to the host time before uploading the XNU payload")
    args = parser.parse_args()
    build(args.base.resolve(), args.firmware.resolve(), args.output.resolve(),
          args.probe, args.probe_mode, args.metal_bundle, args.trace_ui, args.host_clock)
