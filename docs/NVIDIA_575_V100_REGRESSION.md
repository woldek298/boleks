# NVIDIA 575 / Tesla V100 performance regression notes

## Observed symptom

On the same Tesla V100 hardware, the miner has been observed at about 50 CPD with
NVIDIA 550.163.01 and about 33-38 CPD with NVIDIA 575.64.05.

Example telemetry shows the slowdown is dominated by GPU-side work, especially the
sieve phase:

| Driver class | CPD | hashmod | CPU postprocess | sieve | fermat | copy/sync |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 550.163.01 | ~50 | 4.122 ms | 0.030 ms | 76.004 ms | 39.146 ms | 75.658 ms |
| 575.64.05 | ~38 | 4.311 ms | 0.031 ms | 108.993 ms | 46.274 ms | 108.628 ms |

Because CPU postprocess is unchanged and `copy/sync` tracks the longer sieve wait,
the first suspect is not CPU speed but a driver/platform behavior change affecting
GPU work and GPU/host synchronization.

## Driver-release change most relevant to V100

NVIDIA's R575 data-center release notes add a known issue and workaround for
"Disable GPU initiated RO traffic on Ada Lovelace and older GPUs". V100 is a
Volta GPU and is therefore older than Ada. The release notes describe that, on
some platforms and especially where topology is hidden by virtualization, the
mitigation may not be applied automatically. NVIDIA's documented workaround is to
enable persistence mode and clear bit 4 (`RlxdOrd`) in the PCIe Device Control
register with `setpci`.

This repository includes `tools/nvidia_575_v100_workaround.sh` to make that
workaround repeatable for affected V100 hosts.

## Usage

Dry run:

```bash
tools/nvidia_575_v100_workaround.sh
```

Apply workaround:

```bash
sudo tools/nvidia_575_v100_workaround.sh --apply
```

Then restart the miner and compare telemetry. The important value is whether the
575 run moves `sieve` and `copy/sync` closer to the 550 baseline.

## If this does not recover performance

If disabling PCIe Relaxed Ordering does not move the telemetry, the remaining
most likely cause is a closed-driver scheduling/DVFS regression in R575 for V100.
In that case the practical production fix is to pin hosts used for V100 mining to
NVIDIA 550.163.01 or test later R580/R590 branches for a fixed regression.
