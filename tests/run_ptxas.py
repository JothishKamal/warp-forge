#!/usr/bin/env python3
"""Static PTX validation with NVIDIA's standalone assembler.

Assembles every *.ptx in a directory with `ptxas -arch=<sm>` and reports
ptxas's own diagnostics on failure. Exits 77 (CTest: skipped) when ptxas is
not installed, so machines without the CUDA Toolkit still pass the suite.

    run_ptxas.py <ptx-dir> [--arch sm_86] [--ptxas PATH]
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

SKIP = 77


def find_ptxas(explicit):
    if explicit:
        return explicit
    found = shutil.which("ptxas")
    if found:
        return found
    cuda = os.environ.get("CUDA_PATH")
    if cuda:
        candidate = pathlib.Path(cuda) / "bin" / ("ptxas.exe" if os.name == "nt" else "ptxas")
        if candidate.exists():
            return str(candidate)
    return None


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    ptx_dir = pathlib.Path(args[0])
    arch = args[args.index("--arch") + 1] if "--arch" in args else "sm_86"
    ptxas = find_ptxas(args[args.index("--ptxas") + 1] if "--ptxas" in args else None)
    if not ptxas:
        print("SKIP: ptxas not found (install the CUDA Toolkit or set CUDA_PATH)")
        return SKIP

    version = subprocess.run([ptxas, "--version"], capture_output=True, text=True).stdout.strip().splitlines()
    print(f"using {ptxas}: {version[-1] if version else '?'}")
    failures = 0
    files = sorted(ptx_dir.glob("*.ptx"))
    with tempfile.TemporaryDirectory() as tmp:
        for ptx in files:
            out = pathlib.Path(tmp) / (ptx.stem + ".cubin")
            proc = subprocess.run([ptxas, f"-arch={arch}", "-o", str(out), str(ptx)],
                                  capture_output=True, text=True)
            if proc.returncode != 0:
                failures += 1
                print(f"FAIL {ptx.name}:\n{proc.stdout}{proc.stderr}")
            else:
                print(f"ok   {ptx.name}")
    print(f"{len(files)} PTX files assembled with ptxas -arch={arch}, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
