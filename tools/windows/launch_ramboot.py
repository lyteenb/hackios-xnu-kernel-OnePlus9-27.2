#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Start, stop and inspect the device-bound OnePlus 9 RAM boot package."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / 'manifest.json'


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def command(argv, timeout=40, check=True):
    try:
        result = subprocess.run([str(x) for x in argv], capture_output=True,
                                text=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f'命令超时: {argv[0]}') from exc
    text = (result.stdout or '') + (result.stderr or '')
    if check and result.returncode:
        raise SystemExit(f'命令失败: {argv[0]} ({result.returncode})\n{text}')
    return text


def verify() -> dict:
    manifest = json.loads(MANIFEST.read_text())
    if manifest.get('profile') != 'lemonade-ramboot-v1':
        raise SystemExit('不是 OnePlus 9 RAM boot 发布包')
    required = {'boot.img', 'kernel', 'root.apfs', 'system.apfs', 'mkpayload.py'}
    if not required.issubset(manifest.get('files', {})):
        raise SystemExit('发布包文件清单不完整')
    expected_identity = manifest.get('hardware_serial_sha256', '')
    if len(expected_identity) != 64 or any(c not in '0123456789abcdef' for c in expected_identity):
        raise SystemExit('发布包设备身份无效')
    for name in required:
        path = HERE / name
        entry = manifest['files'][name]
        if not path.is_file() or path.is_symlink():
            raise SystemExit('发布包缺少文件: ' + name)
        if path.stat().st_size != entry['bytes']:
            raise SystemExit('文件大小不匹配: ' + name)
        print(f'  {name}: {path.stat().st_size} bytes', flush=True)
        if digest(path) != entry['sha256']:
            raise SystemExit('文件校验失败: ' + name)
    if not manifest.get('boot_arguments'):
        raise SystemExit('发布包没有启动参数')
    return manifest


def adb_devices(manifest):
    text = command(['adb', 'devices', '-l'], check=False)
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] == 'device':
            serial = parts[0]
            if hashlib.sha256(serial.encode()).hexdigest() == manifest['hardware_serial_sha256']:
                return serial
    return None


def fastboot_devices(manifest):
    text = command(['fastboot', 'devices'], check=False)
    found = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] == 'fastboot' and not set(parts[0]) == {'?'}:
            found.append(parts[0])
    return found


def product(serial):
    text = command(['fastboot', '-s', serial, 'getvar', 'product'], timeout=8, check=False)
    for line in text.splitlines():
        if 'product:' in line:
            return line.split('product:', 1)[1].strip()
    return None


def loader(manifest):
    for serial in fastboot_devices(manifest):
        if product(serial) == 'xnuloader':
            return serial
    return None


def wait_for(fn, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = fn()
        if value:
            return value
        time.sleep(1)
    return None


def fb(serial, *args, timeout=40, check=True):
    return command(['fastboot', '-s', serial, *args], timeout=timeout, check=check)


def boot_target(manifest):
    serial = loader(manifest)
    if serial:
        return serial
    android = adb_devices(manifest)
    if not android:
        raise SystemExit('没有找到已绑定的 OnePlus 9（ADB 或 xnuloader）')
    command(['adb', '-s', android, 'reboot', 'bootloader'], timeout=30)
    serial = wait_for(lambda: next((s for s in fastboot_devices(manifest)
                                    if product(s) in ('lemonade', 'OnePlus9', 'LE2110', 'lahaina')), None), 60)
    if not serial:
        raise SystemExit('手机没有进入 fastboot')
    fb(serial, 'boot', HERE / 'boot.img', timeout=60)
    serial = wait_for(lambda: next((s for s in fastboot_devices(manifest)
                                    if product(s) == 'xnuloader'), None), 60)
    if not serial:
        raise SystemExit('加载器 USB 接口未出现')
    return serial


def start(manifest):
    serial = loader(manifest)
    if serial and 'running' in fb(serial, 'getvar', 'result', check=False):
        raise SystemExit('系统已经在运行；先执行 stop 或 android')
    serial = boot_target(manifest)
    print('上传系统镜像…', flush=True)
    fb(serial, 'oem', 'ramdisk')
    fb(serial, 'stage', HERE / 'system.apfs', timeout=360)
    with tempfile.TemporaryDirectory(prefix='lemonade-ramboot-') as temporary:
        payload = Path(temporary) / 'payload.bin'
        command([sys.executable, HERE / 'mkpayload.py', '-o', payload,
                 'kernel=' + str(HERE / 'kernel'), 'ramdisk=' + str(HERE / 'root.apfs'),
                 '--cmdline', ' '.join(manifest['boot_arguments'])], timeout=120)
        if payload.stat().st_size > 0x03f00000:
            raise SystemExit('启动载荷超过加载器上限')
        fb(serial, 'stage', payload, timeout=90)
    fb(serial, 'oem', 'run', '0')
    print('已开始启动；iOS 运行中也可用 status/log/stop 检查。', flush=True)


def stop(manifest, android=False):
    serial = loader(manifest)
    if not serial:
        raise SystemExit('当前没有 xnuloader 接口')
    result = fb(serial, 'getvar', 'result', check=False)
    if 'running' in result:
        fb(serial, 'oem', 'stop')
        result = wait_for(lambda: (r if 'returned' in (r := fb(serial, 'getvar', 'result', check=False)) else None), 60)
        if not result:
            raise SystemExit('系统没有在 60 秒内返回加载器')
    if android:
        fb(serial, 'reboot')
        serial = wait_for(lambda: adb_devices(manifest), 120)
        if not serial or wait_for(lambda: command(['adb', '-s', serial, 'shell', 'getprop', 'sys.boot_completed'], check=False).strip() == '1', 90) is None:
            raise SystemExit('已请求重启，但未确认 Android 启动完成')
        print('已返回 Android；sys.boot_completed=1。', flush=True)
    else:
        print('已退出到 xnuloader。', flush=True)


def show_status(manifest):
    serial = loader(manifest)
    if serial:
        print('loader:', serial)
        print(fb(serial, 'getvar', 'result', check=False))
    elif adb_devices(manifest):
        print('Android USB 在线')
    else:
        print('没有找到目标设备')


def main():
    global HERE, MANIFEST
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('verify', 'start', 'stop', 'android', 'status', 'log'))
    parser.add_argument('--folder', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    HERE = args.folder.resolve()
    MANIFEST = HERE / 'manifest.json'
    manifest = verify()
    if args.action == 'start':
        start(manifest)
    elif args.action in ('stop', 'android'):
        stop(manifest, args.action == 'android')
    elif args.action == 'status':
        show_status(manifest)
    elif args.action == 'log':
        serial = loader(manifest)
        if not serial:
            raise SystemExit('当前没有 xnuloader 接口')
        fb(serial, 'oem', 'log')
        out = HERE / (time.strftime('%Y%m%d-%H%M%S') + '.log')
        fb(serial, 'get_staged', out, timeout=60)
        print(out)
    else:
        print(manifest.get('version', 'lemonade-ramboot') + ': 校验通过')


if __name__ == '__main__':
    main()
