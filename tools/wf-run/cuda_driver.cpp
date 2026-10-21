#include "cuda_driver.hpp"
#include <type_traits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace wfrun {

namespace {

void* openDriver() {
#ifdef _WIN32
  return reinterpret_cast<void*>(LoadLibraryA("nvcuda.dll"));
#else
  void* h = dlopen("libcuda.so.1", RTLD_NOW);
  return h ? h : dlopen("libcuda.so", RTLD_NOW);
#endif
}

void* symbol(void* lib, const char* name) {
#ifdef _WIN32
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
  return dlsym(lib, name);
#endif
}

} // namespace

bool CudaDriver::load(std::string& error) {
  void* lib = openDriver();
  if (!lib) {
    error = "CUDA driver library not found (nvcuda.dll / libcuda.so.1); is an NVIDIA driver installed?";
    return false;
  }
  bool ok = true;
  auto bind = [&](auto& fn, const char* name) {
    void* p = symbol(lib, name);
    if (!p) {
      error = std::string("CUDA driver is missing entry point ") + name;
      ok = false;
    }
    fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
  };
  // The _v2 names are the 64-bit-pointer versions cuda.h maps to by default.
  bind(cuInit, "cuInit");
  bind(cuDriverGetVersion, "cuDriverGetVersion");
  bind(cuDeviceGet, "cuDeviceGet");
  bind(cuDeviceGetName, "cuDeviceGetName");
  bind(cuDeviceGetAttribute, "cuDeviceGetAttribute");
  bind(cuCtxCreate, "cuCtxCreate_v2");
  bind(cuCtxDestroy, "cuCtxDestroy_v2");
  bind(cuCtxSynchronize, "cuCtxSynchronize");
  bind(cuModuleLoadDataEx, "cuModuleLoadDataEx");
  bind(cuModuleUnload, "cuModuleUnload");
  bind(cuModuleGetFunction, "cuModuleGetFunction");
  bind(cuMemAlloc, "cuMemAlloc_v2");
  bind(cuMemFree, "cuMemFree_v2");
  bind(cuMemcpyHtoD, "cuMemcpyHtoD_v2");
  bind(cuMemcpyDtoH, "cuMemcpyDtoH_v2");
  bind(cuLaunchKernel, "cuLaunchKernel");
  bind(cuEventCreate, "cuEventCreate");
  bind(cuEventRecord, "cuEventRecord");
  bind(cuEventSynchronize, "cuEventSynchronize");
  bind(cuEventElapsedTime, "cuEventElapsedTime");
  bind(cuEventDestroy, "cuEventDestroy_v2");
  bind(cuGetErrorString, "cuGetErrorString");
  return ok;
}

std::string CudaDriver::errorString(CUresult r) const {
  const char* s = nullptr;
  if (cuGetErrorString && cuGetErrorString(r, &s) == CUDA_SUCCESS && s) return s;
  return "CUDA error " + std::to_string(r);
}

} // namespace wfrun
