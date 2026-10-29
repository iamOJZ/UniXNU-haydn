#!/usr/bin/env python3
"""Build the storage ABI archive for the haydn UFS link.

NativeUfsBlock subclasses IOBlockStorageDevice, whose implementation lives in the
IOStorageFamily kext rather than in the kernel, so a kernel link needs the class
symbols from somewhere. Upstream satisfies this by binding the driver to recorded
addresses inside the shipped prelinked kernel (build-virtio-mmio-bindings.py plus
interface-audit.json), which needs a macOS dwarfdump pass and a real on-device
recording.

Instead, this builds Apple's own public IOBlockStorageDevice.cpp from the pinned
refer/IOStorageFamily tree (revision e45fbe30..., the exact revision
audit-storage-interface.py insists on) against the exported IOKit headers. The
result is the genuine class implementation, not a placeholder: same source, same
revision, same vtable layout.

It does not make the transport qualified: the audited ABI record is still what
proves the shipped binary matches. It only removes the link-time dependency.

The archive lands in build/haydn-storage-abi/libHaydnStorageABI.a, which the
defconfig adds to the kernel link through CONFIG_XNU_EXTRA_LD_ARCHIVES.
"""
import json
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
AUDITED_REVISION = "e45fbe30a7879e0b16e26dddb78538f1b00e0bff"


def find_tree():
    if (ROOT / "build/xnu/ios-obj").is_dir():
        return ROOT
    override = os.environ.get("UNIXNU_TREE")
    if override:
        return pathlib.Path(override)
    unixnu = ROOT.parent / "UniXNU"
    candidates = sorted(
        list(unixnu.glob("out/*/*/*/tree")) + list(unixnu.glob("out/*/*/tree")),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise SystemExit("no build tree found; set UNIXNU_TREE")
    return candidates[0]


TREE = find_tree()
REFER = TREE / "refer/IOStorageFamily"
SOURCE = REFER / "IOBlockStorageDevice.cpp"
RECORD = TREE / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json"
HEADERS = TREE / "build/storage-abi/include"
OUT = TREE / "build/haydn-storage-abi"
ARCHIVE = OUT / "libHaydnStorageABI.a"

if not SOURCE.is_file():
    raise SystemExit(f"missing {SOURCE}; fetch refer/IOStorageFamily first")
if not HEADERS.is_dir():
    raise SystemExit(f"missing {HEADERS}; prepare-storage-headers must run first")
if not RECORD.is_file():
    raise SystemExit(f"missing compile record {RECORD}; run a kernel build first")

revision = subprocess.check_output(["git", "-C", str(REFER), "rev-parse", "HEAD"], text=True).strip()
if revision != AUDITED_REVISION:
    raise SystemExit(
        f"refer/IOStorageFamily is {revision}, the audited revision is {AUDITED_REVISION}; "
        "the class layout would not match the shipped binary"
    )

OUT.mkdir(parents=True, exist_ok=True)
record = json.loads(RECORD.read_text().strip().rstrip(","))
arguments = record["arguments"]
object_file = OUT / "IOBlockStorageDevice.o"

command = []
index = 0
while index < len(arguments):
    argument = arguments[index]
    if argument == record["file"]:
        command.append(str(SOURCE))
    elif argument == "-o":
        command += ["-o", str(object_file)]
        index += 1
    elif argument == "-Xclang" and arguments[index + 1].startswith("-W"):
        index += 1
    elif not argument.startswith("-W"):
        command.append(argument)
    index += 1
command += ["-Wall", "-Wno-unused-parameter", "-I", str(HEADERS)]
result = subprocess.run(command, cwd=record["directory"], capture_output=True, text=True)
(OUT / "compile.log").write_text(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit((result.stdout + result.stderr)[-3500:])

# cctools ar, not GNU ar: the kernel links with the Mach-O linker.
_linux_ar = pathlib.Path("/opt/unixnu-linux/CommandLineTools/usr/bin/ar")
ar = [str(_linux_ar)] if _linux_ar.is_file() else ["xcrun", "ar"]
if ARCHIVE.exists():
    ARCHIVE.unlink()
subprocess.run([*ar, "rcs", str(ARCHIVE), str(object_file)], check=True)
print(
    f"haydn-storage-abi: {ARCHIVE} ({ARCHIVE.stat().st_size} bytes) "
    f"IOBlockStorageDevice from refer/IOStorageFamily@{revision[:8]}"
)
