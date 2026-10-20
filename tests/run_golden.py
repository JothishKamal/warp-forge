#!/usr/bin/env python3
"""Golden-output tests for the warpforge CLI.

For every <name>.wf in the source directory, runs the compiler in several
modes and compares stdout byte-for-byte with <golden-dir>/<name>.<ext>:

    --dump-tokens          -> .tokens
    --dump-ast             -> .ast
    --dump-ir -O0          -> .ir
    --dump-ir -O1          -> .opt.ir
    -O1 -o -               -> .ptx
    -O0 -o -               -> .O0.ptx

Each command is run twice and the outputs must match (determinism).

    run_golden.py <warpforge> <source-dir> <golden-dir> [--update]

--update rewrites the golden files from the current output; review the diff
before committing.
"""

import difflib
import pathlib
import subprocess
import sys

MODES = [
    (".tokens", ["--dump-tokens"]),
    (".ast", ["--dump-ast"]),
    (".ir", ["--dump-ir", "-O0"]),
    (".opt.ir", ["--dump-ir", "-O1"]),
    (".ptx", ["-O1", "-o", "-"]),
    (".O0.ptx", ["-O0", "-o", "-"]),
]


def run(compiler, args):
    proc = subprocess.run([compiler] + args, capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(args)} exited {proc.returncode}:\n{proc.stderr.decode()}")
    return proc.stdout.decode()


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    compiler, src_dir, golden_dir = str(pathlib.Path(sys.argv[1]).resolve()), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    update = "--update" in sys.argv[4:]
    golden_dir.mkdir(parents=True, exist_ok=True)

    failures = 0
    checked = 0
    for wf in sorted(src_dir.glob("*.wf")):
        for ext, flags in MODES:
            golden = golden_dir / (wf.stem + ext)
            args = [str(wf)] + flags
            try:
                first = run(compiler, args)
                second = run(compiler, args)
            except RuntimeError as e:
                print(f"FAIL {wf.name} {ext}: {e}")
                failures += 1
                continue
            if first != second:
                print(f"FAIL {wf.name} {ext}: output is not deterministic")
                failures += 1
                continue
            # The source path appears in nothing we print, but normalize just in case.
            if update:
                golden.write_text(first, encoding="utf-8", newline="\n")
                continue
            if not golden.exists():
                print(f"FAIL {wf.name} {ext}: missing golden file {golden} (run with --update)")
                failures += 1
                continue
            expected = golden.read_text(encoding="utf-8")
            if expected != first:
                failures += 1
                print(f"FAIL {wf.name} {ext}:")
                sys.stdout.writelines(
                    difflib.unified_diff(expected.splitlines(True), first.splitlines(True),
                                         str(golden), "actual"))
            checked += 1

    if update:
        print(f"updated golden files in {golden_dir}")
        return 0
    print(f"{checked} golden comparisons, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
