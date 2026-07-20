# CUDA driver dependence and build options

## Short answer

A CUDA miner cannot be made fully independent from the installed NVIDIA driver.
The host driver is always responsible for loading modules, scheduling kernels,
managing GPU memory, PCIe/NVLink transfers, clocks, power states, and the kernel-mode
interface to the GPU.

What can be reduced is only one class of variability: PTX JIT code generation.

## Options

### 1. Runtime NVRTC to PTX (current design)

The current CUDA client compiles kernel sources at runtime with NVRTC and stores a
PTX file. The NVIDIA driver then JIT-compiles PTX to native GPU machine code when
`cuModuleLoadDataEx()` loads the module.

Pros:
- Flexible kernel configuration at startup.
- No `nvcc` requirement at build time.
- Easy support for different compute capabilities.

Cons:
- Driver PTX JIT can differ between 550, 570, 575, etc.
- Performance can change even when the source code and GPU are identical.

### 2. Offline cubin for `sm_70` V100

Compile CUDA kernels ahead of time to native cubin/SASS for Volta (`sm_70`) with a
known-good CUDA toolkit and ship/load that cubin for V100.

Pros:
- Removes most PTX JIT code-generation variability.
- Best option if the slowdown is caused by driver PTX JIT codegen.

Cons:
- Does not remove runtime driver dependence.
- Driver 575 can still be slower in scheduling, memory management, synchronization,
  pinned memory, clocks, power, or kernel launch overhead.
- This project currently generates `config.cu` dynamically, so an offline cubin path
  needs either prebuilt cubins for each supported config or a build-time config matrix.

### 3. Fatbinary with cubin + PTX fallback

Ship a CUDA fatbin containing `sm_70` cubin and PTX fallback.

Pros:
- V100 uses native cubin while unknown GPUs can fall back to PTX.
- Standard CUDA distribution model.

Cons:
- More build-system work.
- Still not immune to runtime driver regressions.

### 4. Static linking of CUDA runtime libraries

Static linking can reduce dependency on user-space CUDA runtime libraries, but it
cannot bundle or replace the NVIDIA kernel driver (`libcuda`/kernel module path).

Pros:
- More reproducible user-space dependencies.

Cons:
- Does not solve driver 575 kernel execution or memory-management regressions.

### 5. Pin a known-good driver

For production V100 mining, if telemetry proves 550.163.01 is faster and all clocks,
power limits, and config are identical, pinning the host to 550.163.01 is the most
reliable fix.

Pros:
- Avoids the closed-driver regression directly.
- Lowest implementation risk.

Cons:
- Depends on provider support.
- Not always possible on rented/cloud hosts.

## Recommendation for this miner

1. Revert pageable host-memory fallback: pageable copies break the current pipeline
   behavior and can starve hash production.
2. Keep pinned host memory for correctness/performance of the current async-copy design.
3. If driver independence is still desired, implement a real offline `sm_70` cubin or
   fatbin path for fixed V100 kernel configurations.
4. If native cubin does not improve NVIDIA 575 performance, the remaining regression is
   in the closed driver runtime path and cannot be fixed purely by compilation.

## Implemented `offlineSm70Cubin` path

The CUDA client now has an optional V100-only offline cubin path controlled from
`xpm/cuda/config.txt`:

```txt
offlineSm70Cubin = "true";
nvccPath = "nvcc";
offlineCompilerFlags = "";
```

When enabled on a compute capability 7.0 GPU, the miner concatenates the generated
CUDA kernel sources after `config.cu` is written and invokes `nvcc -cubin
-arch=sm_70 -O3`. The resulting `kernelxpm_gpu<N>_sm70.cubin` is loaded directly
with `cuModuleLoadDataEx()`.

This removes the PTX-to-SASS JIT compiler from the driver-dependent startup path
for V100. It does not remove dependence on the installed NVIDIA driver for module
loading, scheduling, memory management, synchronization, or clock/power behavior.
