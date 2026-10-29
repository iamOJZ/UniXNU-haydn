#!/usr/bin/env python3
"""Build the haydn PMGR latch + touch archive for the XNU link.

Compiles package/HaydnPlatformPower.cpp (the provider-less ApplePMGRNub) and
package/HaydnTouch.cpp + the shared QcomGeniSpi GeniSpi.c (the FTS5652 touch
controller) with the exact flags XNU used for iokit/IOPlatformExpert.cpo, then
archives everything where build-xnu-4k-baseline.sh force-loads it when
XNU_SHENG_PMGR=1 (build/sheng-platform-power/libShengPlatformPower.a).

Run it from the device repository after one kernel build has produced the
compile record. The tree is found through UNIXNU_TREE, else through the newest
out/*/*/tree below the UniXNU checkout next to this repository.
"""
import json
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]

# In the device repository the sources are in package/; in the build tree they are
# linked to kits/haydn/ (the device package module). Pick whichever exists.
def device_source(name):
    direct = ROOT / "package" / name
    if direct.is_file():
        return direct
    kit = ROOT / "kits" / "haydn" / name
    if kit.is_file():
        return kit
    return direct  # let the compiler report the real path

PMGR_SOURCE = device_source("HaydnPlatformPower.cpp")
TOUCH_SOURCE = device_source("HaydnTouch.cpp")
TOUCH_BOARD = device_source("HaydnTouchBoard.h")


def find_tree():
    # When run by `unixnu build kernel`, the script is copied into tree/scripts and
    # executed with cwd=tree, so ROOT is the tree itself and its compile records are
    # right here. When run manually from the device repository, locate the newest tree.
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
OUT = TREE / "build/sheng-platform-power"
RECORD = TREE / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json"
GENI_SPI = TREE / "drivers/QcomGeniSpi/GeniSpi.c"
ARCHIVE = OUT / "libShengPlatformPower.a"

if not RECORD.is_file():
    raise SystemExit(f"missing compile record {RECORD}; run a kernel build first")
if not PMGR_SOURCE.is_file():
    raise SystemExit(f"missing source {PMGR_SOURCE}")

OUT.mkdir(parents=True, exist_ok=True)

record = json.loads(RECORD.read_text().strip().rstrip(","))
arguments = record["arguments"]


def compile_cpp(source, obj, extra_includes=()):
    command = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == record["file"]:
            command.append(str(source))
        elif argument == "-o":
            command += ["-o", str(obj)]
            index += 1
        elif argument == "-Xclang" and arguments[index + 1].startswith("-W"):
            index += 1
        elif not argument.startswith("-W"):
            command.append(argument)
        index += 1
    command += ["-Wall", "-Wno-unused-parameter", "-I", str(PMGR_SOURCE.parent), *extra_includes]
    subprocess.run(command, cwd=record["directory"], check=True)


def compile_c(source, obj, extra_includes=()):
    command = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == record["file"]:
            command.append(str(source))
        elif argument == "-o":
            command += ["-o", str(obj)]
            index += 1
        elif argument in ("-std=gnu++2b", "--driver-mode=g++", "-xc++"):
            index += 1
            continue
        elif argument == "-Xclang" and arguments[index + 1].startswith("-W"):
            index += 1
        elif not argument.startswith("-W"):
            command.append(argument)
        index += 1
    command += ["-I", str(GENI_SPI.parent), *extra_includes]
    subprocess.run(command, cwd=record["directory"], check=True)


pmgr_obj = OUT / "HaydnPlatformPower.o"
touch_obj = OUT / "HaydnTouch.o"
geni_obj = OUT / "GeniSpi.o"

compile_cpp(PMGR_SOURCE, pmgr_obj)
if TOUCH_SOURCE.is_file() and GENI_SPI.is_file():
    compile_cpp(TOUCH_SOURCE, touch_obj, extra_includes=("-I", str(GENI_SPI.parent)))
    compile_c(GENI_SPI, geni_obj)

# cctools ar, not GNU ar: the kernel links with the Mach-O linker.
_linux_ar = pathlib.Path("/opt/unixnu-linux/CommandLineTools/usr/bin/ar")
# Linux uses the cctools port that ships with the UniXNU toolchain; a macOS host
# has the Xcode archiver behind xcrun.
ar = [str(_linux_ar)] if _linux_ar.is_file() else ["xcrun", "ar"]
if ARCHIVE.exists():
    ARCHIVE.unlink()
objects = [pmgr_obj, touch_obj, geni_obj]
objects = [str(o) for o in objects if o.is_file()]
subprocess.run([*ar, "rcs", str(ARCHIVE), *objects], check=True)
print(f"haydn-pmgr+touch: {ARCHIVE} ({ARCHIVE.stat().st_size} bytes, {len(objects)} objects)")

