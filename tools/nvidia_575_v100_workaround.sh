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

if ! command -v lspci >/dev/null 2>&1; then
  echo "ERROR: lspci not found (install pciutils)" >&2
  exit 1
fi

normalize_bdf() {
  local bdf="$1"
  bdf="${bdf,,}"
  if [[ "$bdf" =~ ^([0-9a-f]{8}):([0-9a-f]{2}):([0-9a-f]{2})\.([0-7])$ ]]; then
    printf '%04x:%s:%s.%s' "0x${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" "${BASH_REMATCH[4]}"
  else
    printf '%s' "$bdf"
  fi
}

find_pcie_cap_offset() {
  local bdf="$1"
  local sysfs_config="/sys/bus/pci/devices/$bdf/config"

  if [[ -r "$sysfs_config" ]]; then
    python3 - "$sysfs_config" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'rb') as f:
    data = f.read(256)
if len(data) < 0x40:
    raise SystemExit(1)
ptr = data[0x34]
visited = set()
while ptr and ptr < len(data) - 1 and ptr not in visited:
    visited.add(ptr)
    cap_id = data[ptr]
    nxt = data[ptr + 1]
    if cap_id == 0x10:
        print(f"0x{ptr:x}")
        raise SystemExit(0)
    ptr = nxt
raise SystemExit(1)
PY
    return
  fi

  local cap_line
  cap_line="$(lspci -s "$bdf" -vv 2>/dev/null | sed -n 's/.*Capabilities: \[\([0-9a-fA-F][0-9a-fA-F]*\)\] Express.*/0x\1/p' | head -n 1)"
  if [[ -n "$cap_line" ]]; then
    printf '%s\n' "$cap_line"
    return
  fi

  return 1
}

read_devctl() {
  local bdf="$1"
  local cap_offset="$2"
  local devctl_offset=$((cap_offset + 8))
  setpci -s "$bdf" "$(printf '%x.w' "$devctl_offset")"
}

clear_relaxed_ordering() {
  local bdf="$1"
  local cap_offset="$2"
  local devctl_offset=$((cap_offset + 8))
  setpci -s "$bdf" "$(printf '%x.w=0x0000:0x0010' "$devctl_offset")"
}

mapfile -t GPUS < <(nvidia-smi --query-gpu=index,pci.bus_id,name,driver_version --format=csv,noheader,nounits)
if [[ ${#GPUS[@]} -eq 0 ]]; then
  echo "ERROR: no NVIDIA GPUs reported by nvidia-smi" >&2
  exit 1
fi

needs_workaround=0
detected_target=0
for row in "${GPUS[@]}"; do
  IFS=',' read -r index raw_bdf name driver <<<"$row"
  index="${index//[[:space:]]/}"
  raw_bdf="${raw_bdf//[[:space:]]/}"
  bdf="$(normalize_bdf "$raw_bdf")"
  name="${name# }"
  driver="${driver//[[:space:]]/}"

  if [[ "$driver" != 575.* || "$name" != *"V100"* ]]; then
    echo "GPU $index ($name, driver $driver): skipping (workaround is targeted at V100 on 575.x)"
    continue
  fi

  detected_target=1

  if ! cap_offset_hex="$(find_pcie_cap_offset "$bdf")"; then
    echo "GPU $index ($name, driver $driver, BDF $bdf): PCI Express capability not found; cannot apply workaround" >&2
    continue
  fi

  needs_workaround=1
  cap_offset=$((cap_offset_hex))
  devctl_offset=$((cap_offset + 8))
  current="$(read_devctl "$bdf" "$cap_offset")"
  echo "GPU $index ($name, driver $driver, BDF $bdf): PCIe cap=$cap_offset_hex DevCtl@$(printf '0x%x' "$devctl_offset")=$current"

  if [[ $APPLY -eq 1 ]]; then
    echo "Applying: nvidia-smi -pm 1"
    nvidia-smi -pm 1 >/dev/null
    echo "Applying: setpci -s $bdf $(printf '%x.w=0x0000:0x0010' "$devctl_offset")"
    clear_relaxed_ordering "$bdf" "$cap_offset"
    updated="$(read_devctl "$bdf" "$cap_offset")"
    echo "GPU $index: PCIe DevCtl after workaround=$updated"
  else
    cat <<EOF
Dry run. To apply for GPU $index run as root or with sufficient capabilities:
  nvidia-smi -pm 1
  setpci -s $bdf $(printf '%x.w=0x0000:0x0010' "$devctl_offset")
EOF
  fi
done

if [[ $needs_workaround -eq 0 ]]; then
  if [[ $detected_target -eq 1 ]]; then
    echo "V100 GPUs on NVIDIA 575.x were detected, but PCI Express capability was hidden/unavailable. Try tools/nvidia_v100_max_clocks.sh next."
  else
    echo "No applicable V100 GPU on NVIDIA 575.x detected. Nothing to apply."
  fi
fi
