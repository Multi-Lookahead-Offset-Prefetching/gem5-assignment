# gem5 X6: Multi-Lookahead Offset Prefetching

This repository contains a COA assignment implementation of the X6 / MLOP
paper in gem5, a beginner-friendly paper guide, an implementation report, and
reproducible small-workload results. It is a **submission overlay**, not a full
copy of gem5. The patch was made against upstream gem5 commit
`f5c5a6e390f55dd5984977815bf9d0bd05da6945` (gem5 25.1.0.1).

## Start here

- [Beginner study guide](gem5_assignment/X6_beginner_study_guide.md): plain-language explanation of the paper.
- [Implementation report](gem5_assignment/X6_MLOP_implementation_report.md): algorithm, design choices, changed files, measurements, and limitations.
- [Paper PDF](gem5_assignment/papers/X6_Multi-Lookahead_Offset_Prefetching.pdf).
- [gem5 patch](patches/mlop.patch): the four source changes in a form that can be applied to the matching gem5 revision.

The `gem5/` directory also contains those four final source files for easy
inspection. Apply the patch to a clean gem5 checkout; do not copy this
repository's `gem5/` directory over a complete checkout.

## Reproduce

Install gem5's build dependencies and an X86 C/C++ toolchain, plus `gcc` for
the small benchmark. From a parent directory containing this repository:

```bash
git clone https://github.com/gem5/gem5.git gem5-upstream
cd gem5-upstream
git checkout f5c5a6e390f55dd5984977815bf9d0bd05da6945
git apply ../gem5-assignment/patches/mlop.patch
scons build/X86/gem5.opt -j4
cd ..
GEM5_DIR="$PWD/gem5-upstream" ./gem5-assignment/gem5_assignment/mlop_eval/run_comparison.sh
```

Replace `gem5-assignment` in these commands if you cloned this repository
under another name. If the built gem5 binary cannot find its Python shared
library, set `GEM5_PYTHON_LIB` to the directory containing that library when
running the script. The script compiles its static benchmark and runs three
patterns with no L1 data prefetcher, gem5 BOP, and MLOP.

The saved evidence for all nine runs is in
[`gem5_assignment/mlop_eval/results/`](gem5_assignment/mlop_eval/results/).
The small synthetic workload produced MLOP speedups of 1.814× (sequential),
1.818× (backward), and 1.547× (mixed) relative to no L1 data prefetcher.
These are local simulation results, not the paper's published averages.

## Repository contents

| Path | Purpose |
| --- | --- |
| `gem5/src/mem/cache/prefetch/mlop.hh` and `mlop.cc` | MLOP SimObject C++ implementation |
| `gem5/src/mem/cache/prefetch/Prefetcher.py` and `SConscript` | gem5 registration and parameters |
| `patches/mlop.patch` | Apply the implementation to the pinned gem5 checkout |
| `gem5_assignment/mlop_eval/stream.c` | Synthetic memory-access workload |
| `gem5_assignment/mlop_eval/run_comparison.sh` | Build and run the comparison |
| `gem5_assignment/mlop_eval/results/` | `stats.txt` and `config.ini` for each run |
| `gem5_assignment/X6_MLOP_implementation_report.md` | Full assignment report |
| `gem5_assignment/X6_beginner_study_guide.md` | Paper walkthrough |

The design choices and limits of this interpretation are recorded in the
report. In particular, the paper does not specify every simulator integration
detail, and the included evaluation is smaller than the original DPC3 study.
