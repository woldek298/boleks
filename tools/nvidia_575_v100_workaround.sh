#!/usr/bin/env bash
set -euo pipefail

APPLY=0
if [[ "${1:-}" == "--apply" ]]; then
  APPLY=1
elif [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: tools/nvidia_575_v100_workaround.sh [--apply]

Diagnose and optionally apply the NVIDIA R575/V100 PCIe Relaxed Ordering workaround
recommended in NVIDIA data-center driver release notes for Ada Lovelace and older GPUs.

Without --apply the script prints the current values and the commands it would run.
With --apply it enables persistence mode and clears the PCIe Device Control Relaxed
Ordering bit for Tesla V100 GPUs when the loaded NVIDIA driver is 575.x.
EOF
  exit 0
fi

if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "ERROR: nvidia-smi not found" >&2
  exit 1
fi

if ! command -v setpci >/dev/null 2>&1; then
  echo "ERROR: setpci not found (install pciutils)" >&2
  exit 1
fi

mapfile -t GPUS < <(nvidia-smi --query-gpu=index,pci.bus_id,name,driver_version --format=csv,noheader,nounits)
if [[ ${#GPUS[@]} -eq 0 ]]; then
  echo "ERROR: no NVIDIA GPUs reported by nvidia-smi" >&2
  exit 1
fi

needs_workaround=0
for row in "${GPUS[@]}"; do
  IFS=',' read -r index bdf name driver <<<"$row"
  index="${index//[[:space:]]/}"
  bdf="${bdf//[[:space:]]/}"
  name="${name# }"
  driver="${driver//[[:space:]]/}"

  if [[ "$driver" != 575.* || "$name" != *"V100"* ]]; then
    echo "GPU $index ($name, driver $driver): skipping (workaround is targeted at V100 on 575.x)"
    continue
  fi

  needs_workaround=1
  current="$(setpci -s "$bdf" CAP_EXP+8.w)"
  echo "GPU $index ($name, driver $driver, BDF $bdf): PCIe DevCtl=$current"

  if [[ $APPLY -eq 1 ]]; then
    echo "Applying: nvidia-smi -pm 1"
    nvidia-smi -pm 1 >/dev/null
    echo "Applying: setpci -s $bdf CAP_EXP+8.w=0x0000:0x0010"
    setpci -s "$bdf" CAP_EXP+8.w=0x0000:0x0010
    updated="$(setpci -s "$bdf" CAP_EXP+8.w)"
    echo "GPU $index: PCIe DevCtl after workaround=$updated"
  else
    cat <<EOF
Dry run. To apply for GPU $index run as root or with sufficient capabilities:
  nvidia-smi -pm 1
  setpci -s $bdf CAP_EXP+8.w=0x0000:0x0010
EOF
  fi
done

if [[ $needs_workaround -eq 0 ]]; then
  echo "No V100 GPU on NVIDIA 575.x detected. Nothing to apply."
fi
