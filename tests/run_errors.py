#!/usr/bin/env python3
"""Diagnostic regression tests.

Each tests/errors/*.wf file declares the diagnostics it must produce:

    // expect-error: LINE:COL: message substring

The test runs `warpforge --check FILE` and requires exit code 1, exactly the
set of LINE:COL locations listed, and that each location's message contains
the given substring.

    run_errors.py <warpforge> <errors-dir>
"""

import pathlib
import re
import subprocess
import sys

EXPECT = re.compile(r"//\s*expect-error:\s*(\d+):(\d+):\s*(.+?)\s*$")
DIAG = re.compile(r"^.*?:(\d+):(\d+): error: (.*)$")


def main():
    compiler, err_dir = str(pathlib.Path(sys.argv[1]).resolve()), pathlib.Path(sys.argv[2])
    failures = 0
    files = sorted(err_dir.glob("*.wf"))
    for wf in files:
        expected = {}
        for line in wf.read_text(encoding="utf-8").splitlines():
            m = EXPECT.search(line)
            if m:
                expected[(int(m[1]), int(m[2]))] = m[3]
        proc = subprocess.run([compiler, "--check", str(wf)], capture_output=True)
        stderr = proc.stderr.decode()
        actual = {}
        for line in stderr.splitlines():
            m = DIAG.match(line)
            if m:
                actual[(int(m[1]), int(m[2]))] = m[3]

        problems = []
        if proc.returncode != 1:
            problems.append(f"exit code {proc.returncode}, expected 1")
        if set(expected) != set(actual):
            problems.append(f"locations {sorted(actual)} != expected {sorted(expected)}")
        for loc, text in expected.items():
            if loc in actual and text not in actual[loc]:
                problems.append(f"{loc[0]}:{loc[1]}: '{actual[loc]}' does not contain '{text}'")
        if not expected:
            problems.append("no expect-error annotations")
        if problems:
            failures += 1
            print(f"FAIL {wf.name}")
            for p in problems:
                print(f"  {p}")
            print("  stderr:\n    " + stderr.replace("\n", "\n    "))
    print(f"{len(files)} diagnostic files, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
