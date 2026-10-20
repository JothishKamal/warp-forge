// nvcc baseline for examples/vector_add.wf: same signature, same semantics.
// extern "C" keeps the entry name unmangled so wf-run can launch it by name.
extern "C" __global__ void vector_add(const float* a, const float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        c[i] = a[i] + b[i];
    }
}
