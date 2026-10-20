// wf-run: load WarpForge PTX through the CUDA driver, run a kernel on the
// GPU, and check it against the CPU reference, or benchmark it.
//
//   wf-run --source k.wf --ptx k.ptx --kernel NAME [--n N] [--block B]
//          [--check] [--bench [--warmup W] [--runs R] [--csv FILE] [--label L]]
//
// The .wf source supplies the kernel's signature (to build arguments) and the
// reference result: it is compiled at -O0 and run on the IR interpreter.
// vector_add is additionally checked against a hand-written C++ loop.
// Inputs are generated deterministically (fixed seed); int parameters get
// n - 37 under --check, so the kernel's bounds check is exercised, and n
// under --bench.
//
// Exit codes: 0 ok, 1 failure/mismatch, 2 usage, 77 no CUDA driver or device
// (CTest treats 77 as "skipped").

#include "cuda_driver.hpp"

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/sema.hpp"
#include "ir/interpreter.hpp"
#include "ir/irgen.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace wfrun;

namespace {

constexpr int kSkip = 77;

struct Args {
  std::string source, ptx, kernel, csv, label = "warpforge";
  uint32_t n = 1u << 20;
  uint32_t block = 256;
  bool check = false, bench = false, verbose = false;
  int warmup = 10, runs = 100;
};

bool readFile(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

int usage(const std::string& msg) {
  std::cerr << "wf-run: error: " << msg << "\n"
            << "usage: wf-run --source k.wf --ptx k.ptx --kernel NAME [--n N] [--block B]\n"
            << "              [--check] [--bench [--warmup W] [--runs R] [--csv FILE] [--label L]] [--verbose]\n";
  return 2;
}

bool parseArgs(int argc, char** argv, Args& a, std::string& error) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto value = [&]() -> const char* {
      if (i + 1 >= argc) {
        error = "option '" + arg + "' needs a value";
        return nullptr;
      }
      return argv[++i];
    };
    const char* v = nullptr;
    if (arg == "--check") a.check = true;
    else if (arg == "--bench") a.bench = true;
    else if (arg == "--verbose") a.verbose = true;
    else if (arg == "--source" || arg == "--ptx" || arg == "--kernel" || arg == "--csv" || arg == "--label" ||
             arg == "--n" || arg == "--block" || arg == "--warmup" || arg == "--runs") {
      if (!(v = value())) return false;
      if (arg == "--source") a.source = v;
      else if (arg == "--ptx") a.ptx = v;
      else if (arg == "--kernel") a.kernel = v;
      else if (arg == "--csv") a.csv = v;
      else if (arg == "--label") a.label = v;
      else if (arg == "--n") a.n = static_cast<uint32_t>(std::stoul(v));
      else if (arg == "--block") a.block = static_cast<uint32_t>(std::stoul(v));
      else if (arg == "--warmup") a.warmup = std::stoi(v);
      else a.runs = std::stoi(v);
    } else {
      error = "unknown option '" + arg + "'";
      return false;
    }
  }
  if (a.source.empty() || a.ptx.empty() || a.kernel.empty()) {
    error = "--source, --ptx and --kernel are required";
    return false;
  }
  if (!a.check && !a.bench) a.check = true;
  return true;
}

// Compiles the .wf source at -O0 and returns the module (reference semantics).
std::unique_ptr<wf::ir::Module> compileReference(const std::string& path, std::string& error) {
  std::string text;
  if (!readFile(path, text)) {
    error = "cannot read '" + path + "'";
    return nullptr;
  }
  wf::SourceFile file(path, text);
  wf::DiagnosticEngine diags(file);
  auto program = wf::parse(wf::lex(file, diags), diags);
  if (!diags.errorCount()) wf::checkProgram(*program, diags);
  if (diags.errorCount()) {
    std::ostringstream out;
    diags.print(out);
    error = "source has errors:\n" + out.str();
    return nullptr;
  }
  return wf::lowerProgram(*program);
}

std::string hex(float f) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%08X", std::bit_cast<uint32_t>(f));
  return buf;
}

// Compares buffers bit-for-bit; prints up to 5 mismatches.
bool compare(const std::vector<std::vector<float>>& expected, const std::vector<std::vector<float>>& got,
             const std::vector<std::string>& names, const char* what) {
  size_t mismatches = 0;
  for (size_t b = 0; b < expected.size(); ++b)
    for (size_t i = 0; i < expected[b].size(); ++i)
      if (std::bit_cast<uint32_t>(expected[b][i]) != std::bit_cast<uint32_t>(got[b][i])) {
        if (mismatches < 5)
          std::cerr << "  mismatch vs " << what << ": %" << names[b] << "[" << i << "] expected "
                    << hex(expected[b][i]) << " (" << expected[b][i] << "), got " << hex(got[b][i]) << " ("
                    << got[b][i] << ")\n";
        ++mismatches;
      }
  if (mismatches) std::cerr << "  " << mismatches << " mismatching elements vs " << what << "\n";
  return mismatches == 0;
}

#define CU(call)                                                                                   \
  do {                                                                                             \
    CUresult r_ = (call);                                                                          \
    if (r_ != CUDA_SUCCESS) {                                                                      \
      std::cerr << "wf-run: " #call " failed: " << cuda.errorString(r_) << "\n";                  \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

} // namespace

int main(int argc, char** argv) {
  Args a;
  std::string error;
  if (!parseArgs(argc, argv, a, error)) return usage(error);

  auto module = compileReference(a.source, error);
  if (!module) {
    std::cerr << "wf-run: " << error << "\n";
    return 1;
  }
  wf::ir::Function* ref = nullptr;
  for (auto& f : module->functions)
    if (f->name == a.kernel) ref = f.get();
  if (!ref) {
    std::cerr << "wf-run: kernel '" << a.kernel << "' not found in " << a.source << "\n";
    return 1;
  }

  std::string ptx;
  if (!readFile(a.ptx, ptx)) {
    std::cerr << "wf-run: cannot read '" << a.ptx << "'\n";
    return 1;
  }

  // ---------------------------------------------------------------- device
  CudaDriver cuda{};
  if (!cuda.load(error)) {
    std::cerr << "wf-run: SKIP: " << error << "\n";
    return kSkip;
  }
  if (cuda.cuInit(0) != CUDA_SUCCESS) {
    std::cerr << "wf-run: SKIP: cuInit failed (no usable NVIDIA GPU?)\n";
    return kSkip;
  }
  CUdevice dev;
  if (cuda.cuDeviceGet(&dev, 0) != CUDA_SUCCESS) {
    std::cerr << "wf-run: SKIP: no CUDA device\n";
    return kSkip;
  }
  char name[256] = {};
  int major = 0, minor = 0, driverVersion = 0;
  cuda.cuDeviceGetName(name, sizeof name, dev);
  cuda.cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
  cuda.cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);
  cuda.cuDriverGetVersion(&driverVersion);
  std::cout << "device: " << name << " (sm_" << major << minor << "), CUDA driver API "
            << driverVersion / 1000 << "." << (driverVersion % 1000) / 10 << "\n";

  CUcontext ctx;
  CU(cuda.cuCtxCreate(&ctx, 0, dev));

  // JIT through the driver, capturing its logs so failures are actionable.
  std::vector<char> errorLog(16384, '\0'), infoLog(16384, '\0');
  int options[] = {CU_JIT_ERROR_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES, CU_JIT_INFO_LOG_BUFFER,
                   CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES, CU_JIT_LOG_VERBOSE};
  void* values[] = {errorLog.data(), reinterpret_cast<void*>(errorLog.size()), infoLog.data(),
                    reinterpret_cast<void*>(infoLog.size()), reinterpret_cast<void*>(1)};
  CUmodule mod;
  CUresult jit = cuda.cuModuleLoadDataEx(&mod, ptx.c_str(), 5, options, values);
  if (jit != CUDA_SUCCESS) {
    std::cerr << "wf-run: driver JIT rejected " << a.ptx << ": " << cuda.errorString(jit) << "\n"
              << errorLog.data() << "\n";
    return 1;
  }
  if (a.verbose && infoLog[0]) std::cout << "JIT info log:\n" << infoLog.data() << "\n";
  CUfunction fn;
  CU(cuda.cuModuleGetFunction(&fn, mod, a.kernel.c_str()));

  // ------------------------------------------------------------- arguments
  std::mt19937 rng(1);
  std::uniform_real_distribution<float> dist(-4.0f, 4.0f);
  std::vector<std::vector<float>> host;
  std::vector<std::string> bufferNames;
  std::vector<wf::ir::KernelArg> refArgs;
  int32_t intArg = static_cast<int32_t>(a.check ? (a.n > 37 ? a.n - 37 : a.n) : a.n);
  host.reserve(ref->args().size());
  for (const auto& p : ref->args())
    if (p->type() == wf::ir::Type::Ptr) {
      std::vector<float> b(a.n);
      for (float& x : b) x = dist(rng);
      host.push_back(std::move(b));
      bufferNames.push_back(p->name);
    }

  std::vector<CUdeviceptr> dbuf(host.size());
  for (size_t i = 0; i < host.size(); ++i) {
    CU(cuda.cuMemAlloc(&dbuf[i], host[i].size() * sizeof(float)));
    CU(cuda.cuMemcpyHtoD(dbuf[i], host[i].data(), host[i].size() * sizeof(float)));
  }
  float floatArg = 1.5f;
  std::vector<void*> params;
  size_t nextBuf = 0;
  for (const auto& p : ref->args()) {
    switch (p->type()) {
    case wf::ir::Type::Ptr: params.push_back(&dbuf[nextBuf++]); break;
    case wf::ir::Type::I32: params.push_back(&intArg); break;
    case wf::ir::Type::F32: params.push_back(&floatArg); break;
    default: break;
    }
  }
  unsigned grid = (a.n + a.block - 1) / a.block;

  int status = 0;
  if (a.check) {
    std::vector<std::vector<float>> inputs = host; // pristine copy for the references
    CU(cuda.cuLaunchKernel(fn, grid, 1, 1, a.block, 1, 1, 0, nullptr, params.data(), nullptr));
    CU(cuda.cuCtxSynchronize());
    std::vector<std::vector<float>> gpu = host;
    for (size_t i = 0; i < gpu.size(); ++i)
      CU(cuda.cuMemcpyDtoH(gpu[i].data(), dbuf[i], gpu[i].size() * sizeof(float)));

    // Reference 1: the IR interpreter on the -O0 IR.
    std::vector<std::vector<float>> interp = inputs;
    nextBuf = 0;
    for (const auto& p : ref->args()) {
      if (p->type() == wf::ir::Type::Ptr) refArgs.emplace_back(&interp[nextBuf++]);
      else if (p->type() == wf::ir::Type::I32) refArgs.emplace_back(intArg);
      else refArgs.emplace_back(floatArg);
    }
    wf::ir::InterpResult r = wf::ir::interpret(*ref, refArgs, grid * a.block);
    if (!r.ok) {
      std::cerr << "wf-run: reference interpreter failed: " << r.error << "\n";
      return 1;
    }
    bool ok = compare(interp, gpu, bufferNames, "IR interpreter");

    // Reference 2: a hand-written loop for the canonical kernel.
    if (a.kernel == "vector_add" && inputs.size() == 3) {
      std::vector<std::vector<float>> manual = inputs;
      for (int32_t i = 0; i < intArg; ++i) manual[2][static_cast<size_t>(i)] = inputs[0][static_cast<size_t>(i)] + inputs[1][static_cast<size_t>(i)];
      ok = compare(manual, gpu, bufferNames, "C++ reference loop") && ok;
    }
    std::cout << (ok ? "PASS " : "FAIL ") << a.kernel << " n=" << a.n << " threads=" << grid * a.block
              << " (bit-exact vs " << (a.kernel == "vector_add" ? "IR interpreter and C++ loop" : "IR interpreter")
              << ")\n";
    if (!ok) status = 1;
  }

  if (a.bench) {
    CUevent start, stop;
    CU(cuda.cuEventCreate(&start, 0));
    CU(cuda.cuEventCreate(&stop, 0));
    for (int i = 0; i < a.warmup; ++i)
      CU(cuda.cuLaunchKernel(fn, grid, 1, 1, a.block, 1, 1, 0, nullptr, params.data(), nullptr));
    CU(cuda.cuCtxSynchronize());
    std::vector<double> ms;
    for (int i = 0; i < a.runs; ++i) {
      CU(cuda.cuEventRecord(start, nullptr));
      CU(cuda.cuLaunchKernel(fn, grid, 1, 1, a.block, 1, 1, 0, nullptr, params.data(), nullptr));
      CU(cuda.cuEventRecord(stop, nullptr));
      CU(cuda.cuEventSynchronize(stop));
      float t = 0;
      CU(cuda.cuEventElapsedTime(&t, start, stop));
      ms.push_back(t);
    }
    std::vector<double> sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    double mean = std::accumulate(ms.begin(), ms.end(), 0.0) / static_cast<double>(ms.size());
    double var = 0;
    for (double t : ms) var += (t - mean) * (t - mean);
    double stddev = std::sqrt(var / static_cast<double>(ms.size()));
    double median = sorted[sorted.size() / 2];
    // Approximate bytes moved: each buffer touched once per element. Exact
    // for vector_add; undercounts kernels that read and write the same
    // buffer (saxpy_const: 1.5x). See docs/benchmarks.md.
    double bytes = static_cast<double>(host.size()) * a.n * sizeof(float);
    double gbps = bytes / (median * 1e-3) / 1e9;
    std::printf("BENCH %s %s n=%u runs=%d min=%.4fms median=%.4fms mean=%.4fms stddev=%.4fms ~%.1f GB/s\n",
                a.label.c_str(), a.kernel.c_str(), a.n, a.runs, sorted.front(), median, mean, stddev, gbps);
    if (!a.csv.empty()) {
      bool exists = std::ifstream(a.csv).good();
      std::ofstream csv(a.csv, std::ios::app);
      if (!exists) csv << "label,kernel,n,block,runs,min_ms,median_ms,mean_ms,stddev_ms,gbps\n";
      csv << a.label << ',' << a.kernel << ',' << a.n << ',' << a.block << ',' << a.runs << ',' << sorted.front()
          << ',' << median << ',' << mean << ',' << stddev << ',' << gbps << '\n';
    }
    cuda.cuEventDestroy(start);
    cuda.cuEventDestroy(stop);
  }

  for (CUdeviceptr p : dbuf) cuda.cuMemFree(p);
  cuda.cuModuleUnload(mod);
  cuda.cuCtxDestroy(ctx);
  return status;
}
