# COA coding assignment: MLOP prefetcher in gem5

**Paper:** Shakerinava et al., *Multi-Lookahead Offset Prefetching*, DPC3 (2019).  
**Implementation:** a new `MLOPPrefetcher` for gem5's classic L1 data cache.  
**Companion explanation:** [X6 beginner study guide](X6_beginner_study_guide.md).  
**Paper PDF:** [X6 paper](papers/X6_Multi-Lookahead_Offset_Prefetching.pdf).

## 1. Assignment goal and scope

The paper proposes choosing a separate best offset for each of 16 lookahead levels, then issuing prefetches in increasing lookahead order. The instructor's pasted reply says any gem5 version and cache configuration are acceptable and that necessary assumptions may be made. This implementation follows the paper's main algorithm and documents choices the four-page paper leaves open. It is a **functional gem5 interpretation**, not a claim of bit-for-bit equivalence with the authors' unpublished DPC3 code.

## 2. Files changed or added

| File | What it does |
| --- | --- |
| [`gem5/src/mem/cache/prefetch/mlop.hh`](../gem5/src/mem/cache/prefetch/mlop.hh) | Declares the MLOP prefetcher, access-map entries, scoring data, winners, and statistics. |
| [`gem5/src/mem/cache/prefetch/mlop.cc`](../gem5/src/mem/cache/prefetch/mlop.cc) | Implements map lookup, multi-lookahead scoring, winner selection, prefetch generation, and duplicate suppression. |
| [`gem5/src/mem/cache/prefetch/Prefetcher.py`](../gem5/src/mem/cache/prefetch/Prefetcher.py) | Adds the configurable `MLOPPrefetcher` SimObject with L1 miss-only defaults. |
| [`gem5/src/mem/cache/prefetch/SConscript`](../gem5/src/mem/cache/prefetch/SConscript) | Registers the new Python SimObject and C++ source with the build. |
| [`gem5_assignment/mlop_eval/stream.c`](mlop_eval/stream.c) | A small sequential-memory benchmark to exercise learning and prefetching. |
| [`gem5_assignment/mlop_eval/run_comparison.sh`](mlop_eval/run_comparison.sh) | Builds the benchmark and runs identical cache configurations with no L1-D prefetcher, BOP, and MLOP. |
| This report | Describes the algorithm, assumptions, validation, and how to reproduce the evaluation. |

Existing local edits in the gem5 checkout, including the previously modified `se.py` and CPU2017 files, were not changed for this assignment.

## 3. How the algorithm works

### 3.1 Training event

The prefetcher is attached to **each core's L1 data cache**, one instance per cache. gem5's prefetch hooks call it for eligible L1-D misses. Load and store misses can contribute, subject to gem5's normal filtering of uncacheable accesses, software prefetches, cache-maintenance operations, and some coalesced writes. Hardware-prefetch requests are explicitly excluded from training so a prefetch cannot teach the prefetcher about itself.

The requested address is converted to a **block number**. The block number is split into a region number and a 0–63 position within that region. Thus one AMT entry describes 64 consecutive aligned blocks. A 256-entry direct-mapped table is indexed by region number. A tag and secure bit distinguish colliding entries. On replacement, the entry's bitmaps and ordered history are cleared.

### 3.2 What each AMT entry stores

- `seen`: a 64-bit bitmap of all positions seen while this region's entry remains resident.
- `recent`: the last 15 positions in **oldest-to-newest order** within that entry.
- `aged`: a 64-bit bitmap of positions known to have occurred *before* the recent 15 accesses.

The extra `aged` mask is an implementation choice. A single `seen` bit cannot distinguish one recent occurrence from an older occurrence of the same block. The mask lets the model exclude recent accesses for a high lookahead while still crediting a genuinely older predecessor. When an AMT entry is evicted, its history is lost, as expected for finite hardware metadata.

### 3.3 Scoring rule

For current position `A` and candidate offset `k`, test whether position `A − k` was seen in the same AMT region. The oldest qualifying occurrence determines a lead distance measured in **accesses to that region**, capped at 16. If the lead is `d`, increment the score for that offset at lookahead levels `1` through `d`. For example, if `A − k` occurred four relevant region accesses earlier, `k` earns one point at levels 1, 2, 3, and 4.

The implementation uses ordinary loops for clarity. It does not model the paper's suggested single-cycle, shifted-bit-vector scoring circuit. This changes the *simulator's host computation cost*, not the simulated cycle timing of the prefetcher.

### 3.4 Winner selection and request priority

After **500 eligible L1-D miss events** per prefetcher instance, each level chooses its highest-scoring offset. Scores then reset, and the chosen offsets remain active while the next 500-event window is learned. An all-zero level has no winner. Ties favor the candidate encountered first: smallest absolute distance, with positive before negative. There are no prefetches on the first 499 training events; the 500th event can use the newly selected winners.

For a later miss to block `A`, the prefetcher tries `A + winner[1]`, then `A + winner[2]`, and so on through level 16. gem5's queue assigns higher priority to lower lookahead levels. Repeated winners producing the same target are emitted once. At most 16 unique candidates are returned per event by default; the parameter can lower that limit. Requests crossing the configured page boundary are skipped. gem5's queued-prefetch framework then filters requests already queued, cached, or outstanding and handles a full queue.

## 4. Choices made where the paper is silent

| Question | Choice in this implementation | Reason / consequence |
| --- | --- | --- |
| Candidate offsets | `+1, -1, +2, -2, …, +16, -16` blocks | Configurable `max_offset`; evaluates both directions while limiting request noise. The paper does not list its exact candidates. |
| AMT lookup | 256 direct-mapped entries | Simple bounded storage; collisions may lose history. |
| Map region | 64 aligned cache blocks | Corresponds directly to a 64-bit access map. |
| Lookahead clock | Accesses within the same AMT entry | The paper says the last 15 accesses are kept **within each bit-vector**. It is not elapsed cycles or the global miss sequence. |
| Repeated block accesses | Keep 15 ordered positions plus an older-occurrence bitmap | Avoids incorrectly removing an earlier qualifying occurrence. |
| Cross-region predecessor | Not scored | The AMT lookup is confined to the current map; this may miss patterns spanning its boundary. |
| Evaluation period | 500 eligible misses per prefetcher instance | Mirrors the paper's 500-access period under its L1-D miss training stream. |
| Counter width | 16-bit unsigned score | A score can rise at most 500 times in a period, so it cannot overflow. |
| Zero score / ties | No winner / shortest offset, positive first | Deterministic and avoids issuing untrained guesses. |
| Duplicate winners | One request per unique target | Avoids redundant traffic. |
| Page crossing | Disallowed | Safe default without translation of a speculative cross-page target. |
| Per-core state | Separate prefetcher object per L1-D cache | Scores, maps, and winners do not leak between cores. |

**Storage note:** The paper reports about 8.47 KB for its 256-entry AMT and less than 12 KB total metadata. This model adds a 64-bit `aged` mask to each entry and stores fixed arrays and scores in ordinary C++ objects. Its *allocated simulator memory* includes padding and `std::vector` bookkeeping, so it must not be reported as exactly matching the paper's physical hardware budget. A bit-level estimate for one entry includes up to 64 tag bits, two control bits, two 64-bit masks, fifteen 6-bit recent positions, and a 4-bit count: about **288 bits**, or **9 KiB for 256 entries**, before the score table, winners, queue, and implementation overhead. This is an estimate, not a synthesized hardware measurement.

## 5. How to build and run

From the `gem5` directory, build the simulator with the new class:

```bash
scons build/X86/gem5.opt -j4
```

From the COA workspace, run the small three-way comparison:

```bash
./gem5_assignment/mlop_eval/run_comparison.sh
```

The script uses the sibling `gem5` directory by default. Set `GEM5_DIR` to
another gem5 checkout when running from a separate submission repository.

If gem5 was linked against a Python library outside the system library path,
set `GEM5_PYTHON_LIB` to that library's directory before running the script.
For this workspace's uv-managed Python 3.12 build, the value is
`/home/ronit/.local/share/uv/python/cpython-3.12.14-linux-x86_64-gnu/lib`.

The script uses `TimingSimpleCPU`, 32 KiB L1 instruction and data caches, and a 256 KiB L2 cache. It tests three access patterns: sequential blocks, backward blocks, and an interleaving of stride-1 and stride-2 streams. For **each** pattern it runs no L1-D prefetcher, BOP, and MLOP with otherwise identical settings. It writes gem5 outputs under `gem5_assignment/mlop_eval/results/<pattern>/<model>/`.

Important stats to inspect in each `stats.txt` include `simInsts`, `simTicks`, `system.cpu.ipc` when present, L1-D miss counts, and prefetcher `pfIssued`, `pfUseful`, `accuracy`, and `coverage`. MLOP additionally records `trainingAccesses`, `evaluationWindows`, `amtReplacements`, `candidates`, and `duplicateCandidates`. When reporting a speedup, use a consistent quantity: for a fixed instruction count, `speedup = baseline_simTicks / method_simTicks`; for IPC, `IPC improvement = method_IPC / baseline_IPC − 1`.

## 6. What this evaluation can and cannot establish

The included benchmark is a **small synthetic test** of integration and regular access patterns. It is not the DPC3 suite. A fuller evaluation would add irregular and repeated-block patterns, representative applications, and multi-core workloads. The paper's reported **56% miss coverage** and **30% average performance improvement** are its DPC3 results, not results from this test. The paper states 57 workloads in one place, while its methodology's explicit 44 retained single-core traces plus 11 mixes totals 55; that discrepancy is unresolved in the short paper.

## 7. Checks performed

The Python SimObject file passed syntax compilation. Both new C++ files passed `clang-format --dry-run --Werror`. The comparison script passed shell syntax checking, and the benchmark compiled and printed `checksum=0` on the host. The targeted MLOP object and full `build/X86/gem5.opt` binary built successfully on this host. All nine gem5 runs completed with `checksum=0` and normal thread-context exit. The tested gem5 reports version **25.1.0.1**. Its generated `config.ini` confirms MLOP has 256 AMT entries, a 500-access evaluation period, maximum offset 16, maximum 16 candidates, and L1 data miss-only notification.

## 8. Measured results from this workspace

All numbers below come from the saved gem5 `stats.txt` files. “Speedup” is no-prefetcher `simTicks` divided by that row's `simTicks` **within the same pattern**. Every model executed the same instruction count for a given pattern. `simTicks` therefore gives a consistent elapsed-simulation-time comparison here.

| Pattern | L1-D prefetcher | `simTicks` | IPC | L1-D demand misses | Speedup vs none | Prefetches issued |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Sequential | None | 1,676,324,500 | 0.064809 | 17,394 | 1.000× | — |
| Sequential | BOP | 1,042,976,500 | 0.104165 | 9,255 | 1.607× | 11,599 |
| Sequential | MLOP | 924,296,500 | 0.117540 | 7,564 | **1.814×** | 11,904 |
| Backward | None | 1,669,812,500 | 0.065085 | 17,396 | 1.000× | — |
| Backward | BOP | 1,039,970,500 | 0.104502 | 9,934 | 1.606× | 13,270 |
| Backward | MLOP | 918,475,500 | 0.118325 | 7,816 | **1.818×** | 12,311 |
| Mixed | None | 950,288,500 | 0.088525 | 9,202 | 1.000× | — |
| Mixed | BOP | 635,577,500 | 0.132359 | 5,023 | 1.495× | 6,630 |
| Mixed | MLOP | 614,427,500 | 0.136915 | 4,218 | **1.547×** | 7,373 |

MLOP completed **15, 15, and 8** learning windows on the sequential, backward, and mixed workloads. gem5 reported **9,831, 9,581, and 4,989 useful prefetches**, respectively. Its measured prefetch accuracy (`pfUseful / pfIssued`) was **82.6%, 77.8%, and 67.7%**. MLOP suppressed many duplicate winning offsets before queue insertion; its `duplicateCandidates` counts were **44,623, 44,789, and 24,951**.

These are short, favorable synthetic streams. They demonstrate that the class is connected to the L1-D miss path, learns offsets, issues prefetches, and reduces execution time here. They do **not** establish a general 81% speedup on applications or reproduce the paper's average. BOP's gem5 configuration permits some cross-page prefetches, while this MLOP configuration deliberately blocks them; the comparison includes that documented design difference. gem5's `prefetcher.coverage` statistic uses its own formula and should not be equated directly with the paper's DPC3 miss-coverage figure.

The per-run evidence is in [`results/sequential`](mlop_eval/results/sequential/), [`results/backward`](mlop_eval/results/backward/), and [`results/mixed`](mlop_eval/results/mixed/). Each model directory has `stats.txt` and `config.ini`.

## 9. Key explanation for a viva

> MLOP observes L1 data misses and asks which earlier block could have predicted each miss using an offset. It records the answer separately for 16 lookahead distances, because a prediction can be useful at one lead time and too late at another. Every 500 misses, it chooses one best offset per lookahead. On future misses, it tries those offsets in order of urgency, suppressing duplicate and unsafe requests. I implemented the access-map history, scoring, selection, and request generation as a separate gem5 L1-D prefetcher. Where the short paper omitted details, I made and documented deterministic choices.
