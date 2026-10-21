| kernel | variant | PTX instr | SASS instr | SASS fingerprint |
|---|---|---|---|---|
| vector_add | warpforge-O0 | 24 | 16 | `2a166fd6` |
| vector_add | warpforge-O1 | 24 | 16 | `2a166fd6` |
| vector_add | nvcc | 22 | 16 | `ade128ad` |
| saxpy_const | warpforge-O0 | 35 | 16 | `9b7f87ca` |
| saxpy_const | warpforge-O1 | 23 | 16 | `9b7f87ca` |
| saxpy_const | nvcc | 20 | 16 | `9b7f87ca` |

| kernel | variant | n | median ms | min ms | stddev ms | GB/s |
|---|---|---|---|---|---|---|
| vector_add | warpforge-O0 | 65536 | 0.0061 | 0.0051 | 0.0091 | 128.0 |
| vector_add | warpforge-O0 | 1048576 | 0.0471 | 0.0451 | 0.0061 | 267.1 |
| vector_add | warpforge-O0 | 16777216 | 0.6646 | 0.6380 | 0.0247 | 302.9 |
| vector_add | warpforge-O1 | 65536 | 0.0061 | 0.0049 | 0.0087 | 128.0 |
| vector_add | warpforge-O1 | 1048576 | 0.0471 | 0.0461 | 0.0061 | 267.1 |
| vector_add | warpforge-O1 | 16777216 | 0.6646 | 0.6379 | 0.0265 | 302.9 |
| vector_add | nvcc | 65536 | 0.0061 | 0.0051 | 0.0093 | 128.0 |
| vector_add | nvcc | 1048576 | 0.0471 | 0.0458 | 0.0020 | 267.1 |
| vector_add | nvcc | 16777216 | 0.6646 | 0.6379 | 0.0244 | 302.9 |
| saxpy_const | warpforge-O0 | 65536 | 0.0061 | 0.0049 | 0.0084 | 85.3 |
| saxpy_const | warpforge-O0 | 1048576 | 0.0471 | 0.0458 | 0.0029 | 178.1 |
| saxpy_const | warpforge-O0 | 16777216 | 0.6696 | 0.6369 | 0.0260 | 200.5 |
| saxpy_const | warpforge-O1 | 65536 | 0.0061 | 0.0051 | 0.0095 | 85.3 |
| saxpy_const | warpforge-O1 | 1048576 | 0.0471 | 0.0459 | 0.0019 | 178.1 |
| saxpy_const | warpforge-O1 | 16777216 | 0.6758 | 0.6390 | 0.0268 | 198.6 |
| saxpy_const | nvcc | 65536 | 0.0061 | 0.0051 | 0.0135 | 85.3 |
| saxpy_const | nvcc | 1048576 | 0.0470 | 0.0451 | 0.0121 | 178.5 |
| saxpy_const | nvcc | 16777216 | 0.6697 | 0.6380 | 0.0257 | 200.4 |
