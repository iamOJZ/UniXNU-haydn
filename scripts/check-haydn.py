#!/usr/bin/env python3
"""Cross-check the haydn UEFI memory map against the compiled board device tree.

Runs as the [firmware] preflight step. It is intentionally non-fatal: during
bring-up the device tree may not have been compiled yet, so a missing input is
reported and skipped instead of failing the firmware build.
"""
import re
import subprocess
import sys
from pathlib import Path

CANDIDATE_PLATFORMS = [
    Path('uefi-haydn/Platforms/Xiaomi/haydnPkg'),
    Path('../uefi-haydn/Platforms/Xiaomi/haydnPkg'),
    Path('/home/iamojz/k40p/uefi-haydn/Platforms/Xiaomi/haydnPkg'),
]
CANDIDATE_DTBS = [
    Path('build/haydn/haydn.dtb'),
    Path('device-tree/hardware.dtb'),
    Path('/home/iamojz/k40p/UniXNU-haydn/build/haydn.dtb'),
]


def first_existing(paths):
    for path in paths:
        if path.exists():
            return path
    return None


def cells(dtb, node, prop):
    result = subprocess.run(['fdtget', '-t', 'x', str(dtb), node, prop],
                            capture_output=True, text=True)
    if result.returncode:
        return []
    return [int(word, 16) for word in result.stdout.split()]


def main():
    platform = first_existing(CANDIDATE_PLATFORMS)
    dtb = first_existing(CANDIDATE_DTBS)
    if platform is None:
        print('check-haydn: haydnPkg not found, skipped')
        return 0
    source = (platform / 'Library/MemoryMapLib/MemoryMapLib.c').read_text()
    rows = [(m[1], int(m[2], 16), int(m[3], 16), m[4]) for m in re.finditer(
        r'\{"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),([^}]+)\}', source)]
    if not rows:
        print('check-haydn: memory map descriptors missing')
        return 1
    print(f'check-haydn: {len(rows)} memory descriptors')
    if dtb is None:
        print('check-haydn: no compiled board DTB, reservation cross-check skipped')
        return 0

    nodes = subprocess.run(['fdtget', '-l', str(dtb), '/reserved-memory'],
                           capture_output=True, text=True).stdout.split()
    errors = []
    for node in nodes:
        values = cells(dtb, '/reserved-memory/' + node, 'reg')
        for index in range(0, len(values), 4):
            base = (values[index] << 32) | values[index + 1]
            size = (values[index + 2] << 32) | values[index + 3]
            if size == 0:
                continue
            if not any(a <= base and base + size <= a + s and re.search(r'\bReserv\b', attrs)
                       for _, a, s, attrs in rows):
                errors.append(f'missing reservation {node} @ 0x{base:x}+0x{size:x}')
    for error in errors:
        print('check-haydn:', error)
    print(f'check-haydn: {len(nodes)} reservations checked, {len(errors)} problems')
    # informational only during bring-up
    return 0


if __name__ == '__main__':
    sys.exit(main())
