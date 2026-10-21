#!/usr/bin/env python3
"""GPU fuzz: random programs through ptxas and the real GPU.

The interpreter-based differential test shares its float evaluator with the
constant folder, so it cannot notice where host semantics differ from the
GPU's (it missed NaN canonicalization). This runner uses the hardware as the
oracle instead:

  for each seed: wf-gen writes fuzz_<seed>.wf; then for -O0 and -O1
    warpforge -> PTX -> ptxas -arch=sm_86 (if found) -> wf-run --check
  wf-run compares the GPU's output bit-for-bit with the IR interpreter.

    run_gpu_fuzz.py <warpforge> <wf-run> <wf-gen> [--count N] [--first S] [--ptxas PATH]

Exits 77 (CTest: skipped) when no CUDA driver/GPU is available.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile

SKIP = 77


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("warpforge")
    ap.add_argument("wf_run")
    ap.add_argument("wf_gen")
    ap.add_argument("--count", type=int, default=150)
    ap.add_argument("--first", type=int, default=1)
    ap.add_argument("--ptxas")
    args = ap.parse_args()
    warpforge, wf_run, wf_gen = (str(pathlib.Path(p).resolve()) for p in (args.warpforge, args.wf_run, args.wf_gen))

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        subprocess.run([wf_gen, str(args.first), str(args.count), str(tmp)], check=True)
        for seed in range(args.first, args.first + args.count):
            src = tmp / f"fuzz_{seed}.wf"
            for level in ("O0", "O1"):
                ptx = tmp / f"fuzz_{seed}.{level}.ptx"
                c = subprocess.run([warpforge, str(src), f"-{level}", "-o", str(ptx)], capture_output=True, text=True)
                if c.returncode != 0:
                    failures.append((seed, level, "warpforge", c.stderr))
                    continue
                if args.ptxas:
                    p = subprocess.run([args.ptxas, "-arch=sm_86", "-o", str(tmp / "x.cubin"), str(ptx)],
                                       capture_output=True, text=True)
                    if p.returncode != 0:
                        failures.append((seed, level, "ptxas", p.stdout + p.stderr))
                        continue
                r = subprocess.run([wf_run, "--source", str(src), "--ptx", str(ptx), "--kernel", "fuzz",
                                    "--check", "--n", "256", "--block", "64"], capture_output=True, text=True)
                if r.returncode == SKIP:
                    print("SKIP: " + r.stderr.strip())
                    return SKIP
                if r.returncode != 0:
                    failures.append((seed, level, "gpu", r.stdout + r.stderr))
        for seed, level, stage, detail in failures[:5]:
            print(f"FAIL seed {seed} -O{level[1]} at {stage}:\n{detail}")
            print((tmp / f"fuzz_{seed}.wf").read_text())
    total = 2 * args.count
    print(f"{total} GPU fuzz runs ({args.count} programs x -O0/-O1), {len(failures)} failures"
          + ("" if args.ptxas else " (ptxas not found: GPU check only)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
