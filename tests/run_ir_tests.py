#!/usr/bin/env python3
"""Pass tests on hand-written IR ("given this IR, expect that IR").

Each tests/ir/<name>.wfir starts with a header line

    ; passes: sccp,dce

The runner executes `warpforge <name>.wfir --passes=<list> --dump-ir` and
compares stdout byte-for-byte with <name>.expected.

    run_ir_tests.py <warpforge> <ir-dir>
"""

import difflib
import pathlib
import re
import subprocess
import sys

HEADER = re.compile(r"^;\s*passes:\s*(\S+)")


def main():
    compiler, ir_dir = str(pathlib.Path(sys.argv[1]).resolve()), pathlib.Path(sys.argv[2])
    failures = 0
    files = sorted(ir_dir.glob("*.wfir"))
    for wfir in files:
        m = HEADER.match(wfir.read_text(encoding="utf-8"))
        if not m:
            print(f"FAIL {wfir.name}: missing '; passes: ...' header")
            failures += 1
            continue
        proc = subprocess.run([compiler, str(wfir), f"--passes={m[1]}", "--dump-ir"], capture_output=True)
        actual = proc.stdout.decode()
        expected_path = wfir.with_suffix(".expected")
        expected = expected_path.read_text(encoding="utf-8") if expected_path.exists() else None
        if proc.returncode != 0 or actual != expected:
            failures += 1
            print(f"FAIL {wfir.name} (--passes={m[1]}, exit {proc.returncode})")
            if proc.stderr:
                print(proc.stderr.decode())
            if expected is None:
                print(f"  missing {expected_path.name}")
            else:
                sys.stdout.writelines(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True),
                                                           expected_path.name, "actual"))
    print(f"{len(files)} IR pass tests, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
