// nvcc baseline for examples/saxpy_const.wf (after constant folding, the
// kernel is y[i] = 3.0f * x[i] + y[i]). Compiled with -fmad=false so nvcc
// keeps the multiply and the add separately rounded, like WarpForge; with
// FMA contraction the results would differ in the last bit.
extern "C" __global__ void saxpy_const(const float* x, float* y, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        y[i] = 3.0f * x[i] + y[i];
    }
}
