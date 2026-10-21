#pragma once

// Minimal, dynamically loaded binding to the CUDA Driver API.
//
// wf-run needs only a dozen entry points, and the driver library (nvcuda.dll
// / libcuda.so.1) ships with the NVIDIA display driver, not with the CUDA
// Toolkit. Declaring the few types and functions here and loading them at run
// time means wf-run builds on any machine and needs no toolkit to run: just a
// driver and a GPU.
//
// The PTX goes to the driver through cuModuleLoadDataEx, which invokes the
// driver's built-in JIT compiler (the "Driver Compiler Interface"): the same
// path a CUDA application takes when it ships PTX instead of SASS.

#include <cstddef>
#include <cstdint>
#include <string>

namespace wfrun {

using CUresult = int;
using CUdevice = int;
using CUdeviceptr = unsigned long long;
using CUcontext = struct CUctx_st*;
using CUmodule = struct CUmod_st*;
using CUfunction = struct CUfunc_st*;
using CUevent = struct CUevent_st*;
using CUstream = struct CUstream_st*;

constexpr CUresult CUDA_SUCCESS = 0;

// CUjit_option values (cuda.h)
constexpr int CU_JIT_INFO_LOG_BUFFER = 3;
constexpr int CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES = 4;
constexpr int CU_JIT_ERROR_LOG_BUFFER = 5;
constexpr int CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES = 6;
constexpr int CU_JIT_LOG_VERBOSE = 12;

// CUdevice_attribute values (cuda.h)
constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75;
constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76;

struct CudaDriver {
  CUresult (*cuInit)(unsigned);
  CUresult (*cuDriverGetVersion)(int*);
  CUresult (*cuDeviceGet)(CUdevice*, int);
  CUresult (*cuDeviceGetName)(char*, int, CUdevice);
  CUresult (*cuDeviceGetAttribute)(int*, int, CUdevice);
  CUresult (*cuCtxCreate)(CUcontext*, unsigned, CUdevice);
  CUresult (*cuCtxDestroy)(CUcontext);
  CUresult (*cuCtxSynchronize)();
  CUresult (*cuModuleLoadDataEx)(CUmodule*, const void*, unsigned, int*, void**);
  CUresult (*cuModuleUnload)(CUmodule);
  CUresult (*cuModuleGetFunction)(CUfunction*, CUmodule, const char*);
  CUresult (*cuMemAlloc)(CUdeviceptr*, size_t);
  CUresult (*cuMemFree)(CUdeviceptr);
  CUresult (*cuMemcpyHtoD)(CUdeviceptr, const void*, size_t);
  CUresult (*cuMemcpyDtoH)(void*, CUdeviceptr, size_t);
  CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                             unsigned, CUstream, void**, void**);
  CUresult (*cuEventCreate)(CUevent*, unsigned);
  CUresult (*cuEventRecord)(CUevent, CUstream);
  CUresult (*cuEventSynchronize)(CUevent);
  CUresult (*cuEventElapsedTime)(float*, CUevent, CUevent);
  CUresult (*cuEventDestroy)(CUevent);
  CUresult (*cuGetErrorString)(CUresult, const char**);

  // Loads the driver library and resolves every entry point. On failure,
  // returns false and describes why in `error`.
  bool load(std::string& error);

  std::string errorString(CUresult r) const;
};

} // namespace wfrun
