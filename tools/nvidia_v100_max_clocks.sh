#!/usr/bin/env bash
set -euo pipefail

APPLY=0
if [[ "${1:-}" == "--apply" ]]; then
  APPLY=1
elif [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: tools/nvidia_v100_max_clocks.sh [--apply]

Detect Tesla V100 GPUs and print/apply the highest application clocks reported by
nvidia-smi. This is useful when NVIDIA 575 keeps V100 SXM2 cards in lower clocks
or a different P-state than NVIDIA 550 on the same host.
EOF
  exit 0
fi

if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "ERROR: nvidia-smi not found" >&2
  exit 1
fi

best_supported_clock_pair() {
  local index="$1"
  nvidia-smi -i "$index" --query-supported-clocks=memory,graphics --format=csv,noheader,nounits 2>/dev/null |
    awk -F',' '
      {
        mem=$1; gfx=$2;
        gsub(/^[ \t]+|[ \t]+$/, "", mem);
        gsub(/^[ \t]+|[ \t]+$/, "", gfx);
        if (mem+0 > best_mem || (mem+0 == best_mem && gfx+0 > best_gfx)) {
          best_mem=mem+0;
          best_gfx=gfx+0;
        }
      }
      END {
        if (best_mem > 0 && best_gfx > 0) {
          printf "%d,%d\n", best_mem, best_gfx;
        }
      }'
}

mapfile -t GPUS < <(nvidia-smi --query-gpu=index,name,driver_version,pstate,clocks.sm,clocks.mem,power.draw,power.limit,clocks_throttle_reasons.active --format=csv,noheader,nounits)
if [[ ${#GPUS[@]} -eq 0 ]]; then
  echo "ERROR: no NVIDIA GPUs reported by nvidia-smi" >&2
  exit 1
fi

found=0
for row in "${GPUS[@]}"; do
  IFS=',' read -r index name driver pstate sm_clock mem_clock power_draw power_limit throttle <<<"$row"
  index="${index//[[:space:]]/}"
  name="${name# }"
  driver="${driver//[[:space:]]/}"
  pstate="${pstate//[[:space:]]/}"
  sm_clock="${sm_clock//[[:space:]]/}"
  mem_clock="${mem_clock//[[:space:]]/}"
  power_draw="${power_draw## }"
  power_limit="${power_limit## }"
  throttle="${throttle## }"

  if [[ "$name" != *"V100"* ]]; then
    echo "GPU $index ($name): skipping (not V100)"
    continue
  fi

  found=1
  best_pair="$(best_supported_clock_pair "$index" || true)"
  if [[ -z "$best_pair" ]]; then
    echo "GPU $index ($name, driver $driver): supported clocks not available; current pstate=$pstate sm=${sm_clock}MHz mem=${mem_clock}MHz throttle=$throttle" >&2
    continue
  fi

  echo "GPU $index ($name, driver $driver): current pstate=$pstate sm=${sm_clock}MHz mem=${mem_clock}MHz power=${power_draw}/${power_limit}W throttle=$throttle"
  echo "GPU $index: highest supported application clocks: $best_pair (mem,graphics MHz)"

  if [[ $APPLY -eq 1 ]]; then
    echo "Applying: nvidia-smi -pm 1"
    nvidia-smi -pm 1 >/dev/null
    echo "Applying: nvidia-smi -i $index -ac $best_pair"
    nvidia-smi -i "$index" -ac "$best_pair"
  else
    cat <<EOF
Dry run. To apply for GPU $index run as root or with sufficient capabilities:
  nvidia-smi -pm 1
  nvidia-smi -i $index -ac $best_pair
EOF
  fi
done

if [[ $found -eq 0 ]]; then
  echo "No V100 GPU detected."
fi
