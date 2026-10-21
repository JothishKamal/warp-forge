# Benchmarks

**Question:** does WarpForge's PTX run as fast as nvcc's, and does the -O1 pipeline make the GPU code faster?

**Short answer:** WarpForge's PTX runs at the same speed as nvcc's on both kernels, and -O1 doesn't change
GPU speed at all. `ptxas` compiles WarpForge's -O0 and -O1 PTX to **byte-identical machine code**. For
`saxpy_const`, nvcc's output is identical too. The pipeline does shrink the PTX (35 → 23 instructions for
`saxpy_const`), but `ptxas` performs the same optimizations itself. That is the expected result for a front
end that targets PTX, and it is what NVIDIA's own design assumes: front ends emit clean, simple PTX and
`ptxas` owns machine-level optimization.

## Method

Reproduce with:

```sh
python bench/run_bench.py --warpforge build/mingw-gcc/warpforge.exe --wf-run build/mingw-gcc/wf-run.exe \
    --nvcc  "$CUDA_PATH/bin/nvcc.exe"  --ptxas "$CUDA_PATH/bin/ptxas.exe" \
    --cuobjdump "$CUDA_PATH/bin/cuobjdump.exe" \
    --vcvars "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"
```

- **Variants** (same source semantics):
  - `warpforge-O0` and `warpforge-O1`, from `examples/*.wf`
  - `nvcc`, from `bench/*.cu`, with `nvcc -ptx -arch=sm_86 -O3 -fmad=false`. `-fmad=false` gives nvcc the
    same no-contraction float semantics as WarpForge. Without it, nvcc would emit an FMA and the results
    would differ in the last bit.
- **Correctness first:** every variant must pass `wf-run --check` (bit-exact against the CPU reference at
  n = 2²⁰) before it is timed. All six did, including nvcc's.
- **Same launch path:** all variants are loaded the same way, through the driver JIT
  (`cuModuleLoadDataEx`), and launched with 256-thread blocks.
- **Timing:** CUDA events around each launch. 200 warmup launches, then 200 timed launches. The table
  reports median, minimum and standard deviation. Inputs are fixed-seed.
- **Sizes:** 2¹⁶, 2²⁰ and 2²⁴ elements.
- **Static metrics:**
  - PTX instructions: lines ending in `;` that aren't directives.
  - SASS instructions: `ptxas -O3`, then `cuobjdump -sass`, excluding NOP padding and the trailing `BRA`
    self-loop.
  - SASS fingerprint: a hash of the instruction text, so equal fingerprints mean identical machine code.
- **GB/s** is `buffers × n × 4 bytes / median time`. That is exact for `vector_add` (read a, read b,
  write c). For `saxpy_const` it **undercounts by 1.5×**, because `y` is both read and written. The true
  traffic is 3 × n × 4 bytes, which puts the 2²⁴ result at about 300 GB/s, the same as `vector_add`.

### Environment

| | |
|---|---|
| GPU | NVIDIA GeForce RTX 3060 Laptop GPU (GA106, sm_86), driver 610.62 |
| CUDA | Toolkit 13.3 (`ptxas`/`nvcc`), driver API 13.3 |
| Host | Windows 11 Pro 10.0.26300, MinGW-w64 GCC 13.2 (harness built Debug; GPU timing unaffected) |
| Power | Laptop on AC, default power plan; **GPU clocks not locked**, so expect a few % run-to-run noise |
| Date | 2026-10-20 |

## Results

### Static size

| kernel | variant | PTX instr | SASS instr | SASS fingerprint |
|---|---|---|---|---|
| vector_add | warpforge-O0 | 24 | 16 | `2a166fd6` |
| vector_add | warpforge-O1 | 24 | 16 | `2a166fd6` |
| vector_add | nvcc | 22 | 16 | `ade128ad` |
| saxpy_const | warpforge-O0 | 35 | 16 | `9b7f87ca` |
| saxpy_const | warpforge-O1 | 23 | 16 | `9b7f87ca` |
| saxpy_const | nvcc | 20 | 16 | `9b7f87ca` |

- `vector_add` has nothing for -O1 to remove, so its PTX is the same at both levels.
- For `saxpy_const`, -O1 removes 12 PTX instructions:
  - `scale = 2.0 * 1.5` is folded to 3.0
  - the dead `unused` arithmetic is deleted
  - the always-false `lanes < 0` branch and its store are deleted
- `ptxas` removes the same instructions from the -O0 PTX, and ends with the same 16 SASS instructions.
- WarpForge vs nvcc on `vector_add` differs only in the order of the two loads. That follows from nvcc's PTX
  loading `b` first. Same count, same operations.
- The SASS for `saxpy_const` contains `FMUL` + `FADD` and no `FFMA`. This is the `.rn` modifier at work.

### Kernel time

| kernel | variant | n | median ms | min ms | stddev ms | GB/s |
|---|---|---|---|---|---|---|
| vector_add | warpforge-O0 | 65536 | 0.0061 | 0.0051 | 0.0091 | 128.0 |
| vector_add | warpforge-O1 | 65536 | 0.0061 | 0.0049 | 0.0087 | 128.0 |
| vector_add | nvcc | 65536 | 0.0061 | 0.0051 | 0.0093 | 128.0 |
| vector_add | warpforge-O0 | 1048576 | 0.0471 | 0.0451 | 0.0061 | 267.1 |
| vector_add | warpforge-O1 | 1048576 | 0.0471 | 0.0461 | 0.0061 | 267.1 |
| vector_add | nvcc | 1048576 | 0.0471 | 0.0458 | 0.0020 | 267.1 |
| vector_add | warpforge-O0 | 16777216 | 0.6646 | 0.6380 | 0.0247 | 302.9 |
| vector_add | warpforge-O1 | 16777216 | 0.6646 | 0.6379 | 0.0265 | 302.9 |
| vector_add | nvcc | 16777216 | 0.6646 | 0.6379 | 0.0244 | 302.9 |
| saxpy_const | warpforge-O0 | 65536 | 0.0061 | 0.0049 | 0.0084 | 85.3 |
| saxpy_const | warpforge-O1 | 65536 | 0.0061 | 0.0051 | 0.0095 | 85.3 |
| saxpy_const | nvcc | 65536 | 0.0061 | 0.0051 | 0.0135 | 85.3 |
| saxpy_const | warpforge-O0 | 1048576 | 0.0471 | 0.0458 | 0.0029 | 178.1 |
| saxpy_const | warpforge-O1 | 1048576 | 0.0471 | 0.0459 | 0.0019 | 178.1 |
| saxpy_const | nvcc | 1048576 | 0.0470 | 0.0451 | 0.0121 | 178.5 |
| saxpy_const | warpforge-O0 | 16777216 | 0.6696 | 0.6369 | 0.0260 | 200.5 |
| saxpy_const | warpforge-O1 | 16777216 | 0.6758 | 0.6390 | 0.0268 | 198.6 |
| saxpy_const | nvcc | 16777216 | 0.6697 | 0.6380 | 0.0257 | 200.4 |

Raw data: [`bench/results/results.csv`](../bench/results/results.csv) and
[`bench/results/static.csv`](../bench/results/static.csv).

## Reading the numbers

- **All variants are within noise of each other at every size.** That is expected: where the SASS is
  identical, the kernels *are* the same program. The largest gap, `saxpy_const` -O1 vs -O0 at 2²⁴ (0.6758
  vs 0.6696 ms, about 1%), is between byte-identical binaries, so it is measurement noise. It is also smaller
  than one standard deviation.
- **Both kernels are memory-bound.** At 2²⁴, `vector_add` moves 201 MB in 0.66 ms, about 303 GB/s. That is
  close to the RTX 3060 Laptop's 336 GB/s theoretical peak (192-bit GDDR6 at 14 Gbps). Arithmetic is not
  the bottleneck, so no front-end optimization could have shown a speed-up here.
- **At 2¹⁶ the times are launch overhead** (about 6 µs) rather than kernel work.
- **An earlier run used only 20 warmup launches.** It showed one outlier: `saxpy_const -O0` at 2¹⁶ took
  0.026 ms, the first saxpy launch after the context was created. With 200 warmups it disappeared, so it was
  a clock ramp-up artifact on a laptop GPU, not a code difference. It is mentioned here so the methodology
  is honest.

## What this does and doesn't show

- It shows that WarpForge's PTX is **production-quality input for `ptxas`**. Nothing in it makes the
  final machine code worse than nvcc's, for these kernels.
- It does **not** show WarpForge beating nvcc. It doesn't, and the PRD lists that as a non-goal. The value
  of WarpForge's -O1 pipeline here is a smaller, readable PTX module and a demonstrably correct optimizer,
  not GPU speed.
- A kernel where a front-end optimization would matter to `ptxas` (for example, one that needs
  language-level knowledge `ptxas` lacks, such as aliasing) is outside WarpForge's language.
