#!/usr/bin/env python3
"""Verify vendored hashes and reject SDK/cache dependencies in Ninja builds."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

COMPONENT = Path(__file__).resolve().parents[1]
ROOT = COMPONENT.parents[1]


def verify_files():
    count = 0
    for directory in (COMPONENT, ROOT / "component/nimble_config/upstream-1.10.0"):
        manifest = json.loads((directory / "manifest.json").read_text())
        for relative, record in manifest["files"].items():
            path = directory / relative
            expected = record["sha256"] if isinstance(record, dict) else record
            if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise ValueError(f"Vendored input changed: {path}")
            count += 1
    return count


def audit_build(build):
    build = build.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    match = re.search(r"(?m)^TL3238_TOOLCHAIN:PATH=(.+)$", cache)
    if match is None:
        raise ValueError(f"No pinned toolchain in {build}")
    compiler = Path(match[1]).resolve()
    allowed = [ROOT / "component", ROOT / "project", ROOT / "cmake", build, compiler]
    paths = set()
    for record in json.loads((build / "compile_commands.json").read_text()):
        paths.add(Path(record["file"]).resolve())
        args = record.get("arguments") or shlex.split(record["command"])
        for index, arg in enumerate(args):
            if arg in ("-I", "-isystem", "-iquote"):
                paths.add((Path(record["directory"]) / args[index + 1]).resolve())
            elif arg.startswith("-I") and len(arg) > 2:
                paths.add((Path(record["directory"]) / arg[2:]).resolve())
    graph = subprocess.check_output(["ninja", "-C", str(build), "-t", "inputs", "all"], text=True)
    for line in graph.splitlines():
        paths.add((build / line).resolve())
    deps = subprocess.check_output(["ninja", "-C", str(build), "-t", "deps"], text=True)
    for line in deps.splitlines():
        if line.startswith("    "):
            paths.add((build / line.strip()).resolve())
    bad = []
    for path in sorted(paths):
        if any(path.is_relative_to(root) for root in allowed):
            continue
        # CMake's own compiler/platform detection modules are build tools.
        if str(path).startswith(("/usr/share/cmake-", "/usr/bin/", "/bin/")):
            continue
        bad.append(str(path))
    if bad:
        raise ValueError("External build inputs:\n" + "\n".join(bad))
    return {"build": str(build), "input_count": len(paths), "external_sdk_inputs": []}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = {"result": "PASS", "vendored_files": verify_files(),
              "builds": [audit_build(build) for build in args.build]}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
