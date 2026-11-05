#!/usr/bin/env bash
set -euo pipefail

assignment_dir="$(cd "$(dirname "$0")/.." && pwd)"
gem5_dir="${GEM5_DIR:-$(cd "$assignment_dir/../gem5" && pwd)}"
binary="$assignment_dir/mlop_eval/stream"
results_dir="$assignment_dir/mlop_eval/results"

if [[ ! -x "$gem5_dir/build/X86/gem5.opt" ]]; then
    echo "Build gem5.opt first and set GEM5_DIR to that checkout." >&2
    exit 1
fi

# Set GEM5_PYTHON_LIB when gem5 was linked against a Python outside the
# system library path (for example, a uv-managed Python installation).
if [[ -n "${GEM5_PYTHON_LIB:-}" ]]; then
    export LD_LIBRARY_PATH="$GEM5_PYTHON_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

gcc -O2 -static -no-pie -o "$binary" \
    "$assignment_dir/mlop_eval/stream.c"
mkdir -p "$results_dir"

for pattern in sequential backward mixed; do
    for model in none BOPPrefetcher MLOPPrefetcher; do
        output_dir="$results_dir/$pattern/$model"
        extra_args=()
        if [[ "$model" != none ]]; then
            extra_args=("--l1d-hwp-type=$model")
        fi

        "$gem5_dir/build/X86/gem5.opt" --outdir="$output_dir" \
            "$gem5_dir/configs/deprecated/example/se.py" \
            --cpu-type=TimingSimpleCPU --caches --l2cache \
            --l1d_size=32KiB --l1i_size=32KiB --l2_size=256KiB \
            "${extra_args[@]}" --cmd="$binary" --options="$pattern"
    done
done

echo "Results are in $results_dir/<pattern>/<model>/stats.txt"
