#!/usr/bin/env python3
"""Benchmark WarpForge -O0, WarpForge -O1 and nvcc on the same kernels.

For each kernel and variant this script:
  1. produces PTX (warpforge -O0 / -O1, or nvcc -ptx -arch=sm_86 -fmad=false),
  2. checks it on the GPU with `wf-run --check` (bit-exact vs the CPU reference),
  3. records static size: PTX instructions, and SASS instructions after ptxas,
  4. times it with `wf-run --bench` at several sizes (warmup + repeated runs).

    run_bench.py --warpforge PATH --wf-run PATH [--nvcc PATH] [--ptxas PATH]
                 [--cuobjdump PATH] [--vcvars PATH] [--runs N] [--out DIR]

Results: <out>/results.csv (timings), <out>/static.csv (instruction counts),
and a Markdown summary on stdout.
"""

import argparse
import csv
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
KERNELS = ["vector_add", "saxpy_const"]
SIZES = [1 << 16, 1 << 20, 1 << 24]


def sh(cmd, **kw):
    proc = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if proc.returncode != 0:
        raise RuntimeError(f"{cmd}\n{proc.stdout}{proc.stderr}")
    return proc.stdout


def nvcc_ptx(args, cu, out):
    cmd = f'"{args.nvcc}" -ptx -arch=sm_86 -O3 -fmad=false -o "{out}" "{cu}"'
    if os.name == "nt" and args.vcvars:
        # nvcc needs cl.exe on PATH; vcvars64.bat sets that up.
        cmd = f'call "{args.vcvars}" >nul && {cmd}'
        # A raw string: list-style quoting confuses cmd.exe's own parser.
        sh(f'cmd /d /s /c "{cmd}"')
    else:
        sh(cmd, shell=True)


def ptx_instructions(ptx_text):
    count = 0
    for line in ptx_text.splitlines():
        line = line.strip()
        if not line.endswith(";") or line.startswith((".", "//")):
            continue
        count += 1
    return count


def sass(args, ptx_path, tmp):
    """Returns (instruction count, short fingerprint) of ptxas's SASS output.

    Counts real instructions only: the NOP padding and the `BRA` self-loop
    ptxas appends after the final EXIT are excluded. Two variants with the same
    fingerprint compiled to byte-for-byte identical machine code.
    """
    if not (args.ptxas and args.cuobjdump):
        return "", ""
    cubin = pathlib.Path(tmp) / (ptx_path.stem + ".cubin")
    sh([args.ptxas, "-arch=sm_86", "-O3", "-o", str(cubin), str(ptx_path)])
    listing = sh([args.cuobjdump, "-sass", str(cubin)])
    # SASS lines look like:  /*0070*/   IMAD R0, R0, c[0x0][0x0], R3 ;
    insts = [m.strip() for m in re.findall(r"/\*[0-9a-f]{4,}\*/\s+([^;]*);", listing)]
    insts = [i for i in insts if i != "NOP"]
    if insts and insts[-1].startswith("BRA"):
        insts.pop()
    return len(insts), hashlib.sha1("\n".join(insts).encode()).hexdigest()[:8]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--warpforge", required=True)
    ap.add_argument("--wf-run", required=True)
    ap.add_argument("--nvcc")
    ap.add_argument("--ptxas")
    ap.add_argument("--cuobjdump")
    ap.add_argument("--vcvars")
    ap.add_argument("--runs", type=int, default=200)
    ap.add_argument("--warmup", type=int, default=200)
    ap.add_argument("--out", default=str(ROOT / "bench" / "results"))
    args = ap.parse_args()
    # Windows CreateProcess wants absolute executable paths.
    args.warpforge = str(pathlib.Path(args.warpforge).resolve())
    args.wf_run = str(pathlib.Path(args.wf_run).resolve())

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    timings = out / "results.csv"
    if timings.exists():
        timings.unlink()

    static_rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for kernel in KERNELS:
            source = ROOT / "examples" / f"{kernel}.wf"
            variants = {}
            for level in ("O0", "O1"):
                p = pathlib.Path(tmp) / f"{kernel}.{level}.ptx"
                sh([args.warpforge, str(source), f"-{level}", "-o", str(p)])
                variants[f"warpforge-{level}"] = p
            if args.nvcc:
                p = pathlib.Path(tmp) / f"{kernel}.nvcc.ptx"
                nvcc_ptx(args, ROOT / "bench" / f"{kernel}.cu", p)
                variants["nvcc"] = p
                (out / f"{kernel}.nvcc.ptx").write_text(p.read_text(), newline="\n")

            for label, ptx in variants.items():
                check = sh([args.wf_run, "--source", str(source), "--ptx", str(ptx), "--kernel", kernel,
                            "--check", "--n", str(1 << 20)])
                if "PASS" not in check:
                    raise RuntimeError(f"{label} {kernel} failed the GPU check:\n{check}")
                count, fingerprint = sass(args, ptx, tmp)
                static_rows.append({"kernel": kernel, "variant": label,
                                    "ptx_instructions": ptx_instructions(ptx.read_text()),
                                    "sass_instructions": count, "sass_fingerprint": fingerprint})
                for n in SIZES:
                    line = sh([args.wf_run, "--source", str(source), "--ptx", str(ptx), "--kernel", kernel,
                               "--bench", "--n", str(n), "--runs", str(args.runs), "--warmup", str(args.warmup),
                               "--csv", str(timings), "--label", label])
                    print(line.strip().splitlines()[-1], file=sys.stderr)

    with open(out / "static.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["kernel", "variant", "ptx_instructions", "sass_instructions",
                                          "sass_fingerprint"])
        w.writeheader()
        w.writerows(static_rows)

    # Markdown summary
    print("| kernel | variant | PTX instr | SASS instr | SASS fingerprint |")
    print("|---|---|---|---|---|")
    for r in static_rows:
        print(f"| {r['kernel']} | {r['variant']} | {r['ptx_instructions']} | {r['sass_instructions']} "
              f"| `{r['sass_fingerprint']}` |")
    print()
    print("| kernel | variant | n | median ms | min ms | stddev ms | GB/s |")
    print("|---|---|---|---|---|---|---|")
    with open(timings) as f:
        for r in csv.DictReader(f):
            print(f"| {r['kernel']} | {r['label']} | {r['n']} | {float(r['median_ms']):.4f} | "
                  f"{float(r['min_ms']):.4f} | {float(r['stddev_ms']):.4f} | {float(r['gbps']):.1f} |")


if __name__ == "__main__":
    sys.exit(main())
