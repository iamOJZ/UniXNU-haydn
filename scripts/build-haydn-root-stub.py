#!/usr/bin/env python3
"""Build the refusal-only owner-root stub archive for the haydn UFS link.

ShengUfs is compiled with NATIVE_VERIFIED_BLOCK=1 and therefore references the
owner-root symbols (verified_root_policies/_count/_key, vr_open, vr_verify_block,
vr_xnu_crypto) that only libVerifiedRootProbe.a defines. Producing that archive
needs a prior audit of the real OS image, which this board does not have, so
package/HaydnRootStub.cpp defines them as a refusal-only placeholder.

The archive lands in build/haydn-root-stub/libHaydnRootStub.a, which the
defconfig adds to the kernel link through CONFIG_XNU_EXTRA_LD_ARCHIVES.

Run it from the device repository after one kernel build has produced the
compile record. The tree is found through UNIXNU_TREE, else through the newest
out/*/*/tree below the UniXNU checkout next to this repository.
"""
import json
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def device_source(name):
    # In the device repository the sources are in package/; the tree flattens that
    # directory into kits/haydn/ (the device package module). Pick whichever exists.
    for candidate in (ROOT / "package" / name, ROOT / "kits" / "haydn" / name):
        if candidate.is_file():
            return candidate
    return ROOT / "package" / name  # let the compiler report the real path


STUB_SOURCE = device_source("HaydnRootStub.cpp")


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
OUT = TREE / "build/haydn-root-stub"
RECORD = TREE / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json"
ARCHIVE = OUT / "libHaydnRootStub.a"

if not RECORD.is_file():
    raise SystemExit(f"missing compile record {RECORD}; run a kernel build first")
if not STUB_SOURCE.is_file():
    raise SystemExit(f"missing source {STUB_SOURCE}")

OUT.mkdir(parents=True, exist_ok=True)

record = json.loads(RECORD.read_text().strip().rstrip(","))
arguments = record["arguments"]

object_file = OUT / "HaydnRootStub.o"
command = []
index = 0
while index < len(arguments):
    argument = arguments[index]
    if argument == record["file"]:
        command.append(str(STUB_SOURCE))
    elif argument == "-o":
        command += ["-o", str(object_file)]
        index += 1
    elif argument == "-Xclang" and arguments[index + 1].startswith("-W"):
        index += 1
    elif not argument.startswith("-W"):
        command.append(argument)
    index += 1
command += [
    "-Wall",
    "-Wno-unused-parameter",
    "-I", str(TREE / "kits/VerifiedRoot/include"),
    "-I", str(TREE / "drivers/VirtioMmioBlock"),
]
subprocess.run(command, cwd=record["directory"], check=True)

# cctools ar, not GNU ar: the kernel links with the Mach-O linker.
_linux_ar = pathlib.Path("/opt/unixnu-linux/CommandLineTools/usr/bin/ar")
ar = [str(_linux_ar)] if _linux_ar.is_file() else ["xcrun", "ar"]
if ARCHIVE.exists():
    ARCHIVE.unlink()
subprocess.run([*ar, "rcs", str(ARCHIVE), str(object_file)], check=True)
print(
    f"haydn-root-stub: {ARCHIVE} ({ARCHIVE.stat().st_size} bytes) "
    "refusal-only owner-root placeholder (0 policies)"
)
