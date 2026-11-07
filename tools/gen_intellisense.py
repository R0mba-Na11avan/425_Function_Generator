"""
Author: Parikshith Venkataraghavan
Purpose: generates .vscode/compile_commands.json so VS Code IntelliSense uses
         the real ESP32-S3 compiler flags instead of guessing.

arduino-cli can write a compile database, but its entries point at copies of
the sources inside the build folder and hide most flags in @response files
(with -iprefix tricks) that IntelliSense doesn't follow. This script expands
those and points every entry back at the files in this repo.

Run it again after changing the board options or upgrading the ESP32 core:
    python tools/gen_intellisense.py
"""

import json
import os
import shlex
import subprocess
import sys
import tempfile

FQBN = "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(tempfile.gettempdir(), "funcgen_intellisense")
OUT = os.path.join(ROOT, ".vscode", "compile_commands.json")


def read_response_file(path):
    with open(path, encoding="utf-8") as f:
        return shlex.split(f.read())


def expand_args(args):
    #inline every @file, then turn -iprefix P / -iwithprefixbefore X into -I P+X
    expanded = []
    for arg in args:
        if arg.startswith("@") and os.path.isfile(arg[1:]):
            expanded.extend(read_response_file(arg[1:]))
        else:
            expanded.append(arg)

    result = []
    prefix = ""
    i = 0
    while i < len(expanded):
        arg = expanded[i]
        if arg == "-iprefix":
            prefix = expanded[i + 1]
            i += 2
            continue
        if arg in ("-iwithprefixbefore", "-iwithprefix"):
            result.append("-I" + os.path.normpath(prefix + expanded[i + 1]))
            i += 2
            continue
        if arg in ("-o", "-MF"):
            i += 2
            continue
        if arg in ("-MMD", "-c"):
            i += 1
            continue
        result.append(arg)
        i += 1
    return result


def repo_path(build_file):
    #<build>/sketch/<path in repo> -> <repo>/<path in repo>
    sketch_dir = os.path.join(BUILD, "sketch")
    rel = os.path.relpath(build_file, sketch_dir)
    if rel.startswith(".."):
        return None
    if rel.endswith(".ino.cpp"):
        rel = rel[: -len(".cpp")]
    path = os.path.join(ROOT, rel)
    return path if os.path.isfile(path) else None


def main():
    cmd = ["arduino-cli", "compile", "--fqbn", FQBN, "--only-compilation-database",
           "--build-path", BUILD, ROOT]
    print(" ".join(cmd))
    if subprocess.call(cmd) != 0:
        sys.exit("arduino-cli failed")

    with open(os.path.join(BUILD, "compile_commands.json"), encoding="utf-8") as f:
        entries = json.load(f)

    out = []
    for entry in entries:
        args = entry.get("arguments") or shlex.split(entry["command"])
        source = repo_path(args[-1] if args[-1] == entry["file"] else entry["file"])
        if source is None:
            continue #core and library files, IntelliSense doesn't need them
        args = [a for a in expand_args(args) if os.path.normpath(a) != os.path.normpath(entry["file"])]
        out.append({"directory": ROOT, "file": source, "arguments": args + [source]})

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    print(f"wrote {len(out)} entries to {OUT}")


if __name__ == "__main__":
    main()
