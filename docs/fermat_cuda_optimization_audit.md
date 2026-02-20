# Fermat/CUDA performance audit and next-step roadmap

## Scope of analysis

This audit is based on a repository-wide source inventory (`rg --files`) and a hot-path scan (`rg -n`) focused on CUDA launch sites, atomics, synchronization, and candidate pipelines.

Primary focus:
- `xpm/cuda/fermat.cu`
- `xpm/cuda/sieve.cu`
- `xpm/cuda/sha256.cu`
- `xpm/cuda/xpmclient.cpp`
- `xpm/cuda/xpmclient.h`
- `xpm/cuda/benchmarks.cpp`
- `xpm/cuda/benchmarks.cu`
- `xpm/cuda/procs.cu`

Cross-repo context checked for architecture parity and correctness flow:
- `xpm/opencl/*` (kernel architecture counterpart)
- `xpm/rpc/common/*` and `xpm/rpc/include/*` (CPU-side chain validation and test semantics)
- top-level miner integration (`baseclient.cpp`, build files)

---

## Highest-value next optimizations (Fermat/sec)

### 1) Remove expensive modulo/division in sieve position normalization

**Where:** `xpm/cuda/sieve.cu` in both the stage-1 and later sieve loops.

Current code repeatedly does:
- float-assisted estimate (`fentry * fiprime`)
- correction ladders with signed checks and repeated `pos -=/+= prime`

**Proposal:** replace with a fully integer reciprocal-based reduction path (`__umulhi` with precomputed reciprocal and one correction step), similar to the SHA small-prime divisibility trick pattern in `xpm/cuda/sha256.cu`.

**Why:** this section runs for huge prime sets and is memory/atomic pressured; reducing ALU/control overhead before atomics improves total sieve throughput and the number of candidates delivered to Fermat kernels.

**Risk:** low-medium (careful correctness test needed on boundary primes).

---

### 2) Warp-aggregate candidate writes in `s_sieve` (not only in `check_fermat`)

**Where:** `xpm/cuda/sieve.cu`, kernel `s_sieve`.

Current code does per-hit `atomicAdd` into `fcount[]` and immediate global struct writes (`found320/found352`) across 3 scan passes.

**Proposal:** same compaction style already used in `check_fermat`:
- build lane mask of hits,
- one `atomicAdd` per warp per destination queue,
- lane-rank writeout into contiguous segment.

**Why:** this is likely the largest remaining global atomic hotspot in candidate generation path.

**Risk:** medium (needs careful split across 320/352 queues and 3 candidate types).

---

### 3) Asynchronous host flow: reduce blocking `copyToHost` cadence

**Where:** `xpm/cuda/xpmclient.cpp` around stream synchronization and host copies.

Current loop still does frequent blocking synchronization/copy (`cuEventSynchronize`, multiple `copyToHost`) once per iteration.

**Proposal:**
- keep 2+ iteration delayed consumption for counters/results,
- use pinned host staging for only metadata needed now,
- batch count copies and avoid full `final.info` copies when `final.count == 0`.

**Why:** this reduces host-side pipeline bubbles and keeps GPU work queues full; typically gives large stability gains in Fermat/sec under variable candidate density.

**Risk:** medium (requires host control-flow refactor).

---

### 4) Occupancy-aware launch auto-tuner (runtime micro-benchmark)

**Where:** `xpm/cuda/xpmclient.cpp` (`mBlockSize`, block dims 64/256, sieve local size).

Current launch dimensions are static heuristics (`computeUnits * 4 * 64`, fixed block sizes).

**Proposal:** at startup run short warm-up benchmark for combinations:
- Fermat block size (64/128/256),
- sieve local size variants,
- pipeline width (`mSievePerRound`) control policy.

Pick best by measured candidates/sec and Fermat/sec.

**Why:** static values underperform on modern architectures with different SM register/shared-memory limits.

**Risk:** low (self-contained; fallback to defaults).

---

### 5) Register-pressure split for Fermat kernels

**Where:** `xpm/cuda/fermat.cu` + generated arithmetic in `xpm/cuda/procs.cu`.

Current path uses large stack arrays (`uint32_t e[10/11]`, `m[10/11]`, etc.) and heavily unrolled bignum operations, which can reduce occupancy due to register pressure.

**Proposal:**
- test a variant with controlled unroll (selected loops),
- move rarely reused temporaries into smaller scopes,
- build two kernel variants (high-occupancy vs high-ILP) selected by autotuner.

**Why:** Fermat kernels are compute-heavy; occupancy cliffs directly impact total throughput.

**Risk:** medium-high (requires Nsight Compute validation).

---

## Modernization improvements (maintainability + long-term performance)

1. **Type/alias cleanup in CUDA files**
   - Prefer `<cstdint>` style aliases in shared headers and reduce duplicated typedef patterns between `.cu` files.

2. **Kernel name indirection safety**
   - Current mangled-name lookups in `xpm/cuda/xpmclient.cpp` are brittle.
   - Add a compile-time generated manifest or wrapper C API for safer symbol resolution.

3. **Benchmark parity automation**
   - Extend `xpm/cuda/benchmarks.cpp` to emit machine-readable perf snapshots (CSV/JSON) per kernel and config so tuning regressions are visible in CI-like runs.

4. **Cross-backend consistency checks**
   - Keep CUDA/OpenCL behavior aligned for sieve and Fermat candidate semantics (`xpm/opencl/*` vs `xpm/cuda/*`) to prevent backend-specific drift.

---

## Suggested implementation order

1. `s_sieve` warp-aggregated output atomics.
2. Sieve position normalization integer-reciprocal path.
3. Host async copy/sync reduction in `xpmclient.cpp`.
4. Runtime autotuner integration.
5. Fermat register-pressure variants and selection.

This order gives the best expected Fermat/sec gain per engineering effort while keeping correctness risk manageable.
