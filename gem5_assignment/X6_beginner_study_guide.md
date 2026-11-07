# X6 study guide: Multi-Lookahead Offset Prefetching (MLOP)

**Paper:** Mehran Shakerinava, Mohammad Bakhshalipour, Pejman Lotfi-Kamran, and Hamid Sarbazi-Azad, *Multi-Lookahead Offset Prefetching*, Third Data Prefetching Championship (DPC3), 2019.  
**Source:** [X6 paper](papers/X6_Multi-Lookahead_Offset_Prefetching.pdf) (4 pages).  
**How to use this guide:** Read Sections 1–5 for the idea, Section 6 for the mechanism, and Sections 7–9 for the experiment and its limits. The final section contains a ready-to-use ChatGPT tutoring prompt.

## 1. The whole paper in one minute

A processor sometimes needs data that is not in its nearby cache. It then waits for a slower memory level. A **prefetcher** tries to fetch that data before the processor asks for it. An **offset prefetcher** sees a request for cache block `A` and guesses that `A + k` will be needed, where `k` is a distance measured in *cache blocks*.

Earlier offset prefetchers face a trade-off. Prefetching an accurately predicted block too late does little good. Choosing only one very timely offset may miss other useful patterns. MLOP learns which offset works at each of several **lookahead levels**. It then uses one winning offset per level and gives priority to levels whose predicted accesses are nearer in the future. In the authors' DPC3 simulation, MLOP covered an average of **56% of the evaluated single-core cache misses** and improved average performance by **30% versus no data prefetcher**. The paper reports **4% better performance than SPP**, its strongest comparison method for average performance.

## 2. Vocabulary you need before reading

| Term | Simple meaning | Why it matters here |
| --- | --- | --- |
| Cache block or cache line | A fixed-size chunk of memory moved into a cache. | `A + 2` means two *blocks* after `A`, not two bytes after it. |
| Cache hit / miss | The needed block is present / absent in the cache. | A miss can stall the processor while data arrives. |
| L1 data cache (L1-D) | Small, fast cache closest to a core for data. | The compared prefetchers sit at L1-D and learn from its miss stream. |
| Prefetch | Request a block before the demand request arrives. | A useful prefetch can hide part of a future miss. |
| Demand access | An access made by the program itself. | This is what the prefetcher tries to anticipate. |
| Offset `k` | A block-address difference. | A demand for `A` can trigger a prediction for `A + k`. |
| Lookahead `L` | At least `L` accesses between the earlier trigger and the later target, in the paper's scoring sense. | Larger `L` tends to allow more time, but it is **not** a number of cycles or a guarantee that memory will finish. |
| Timeliness | Whether data arrives before the program needs it. | Correct but late prefetches often help little. |
| Miss coverage | Fraction of cache misses that the prefetcher covers. | Coverage can rise even if some extra requests waste bandwidth. |
| Overprediction | Prefetching blocks that are not usefully demanded. | It can waste bandwidth and cache space. |
| IPC | Instructions completed per clock cycle. | The paper uses IPC improvement to measure performance. |

**A physical intuition.** Suppose a program reads blocks `100, 102, 104, 106`. If it reads `100`, a `+2` offset prefetcher asks for `102`. If the memory system returns `102` before the program asks for it, a later miss may be avoided or shortened. If the program asks for `102` almost immediately, the prediction can be correct but too late.

## 3. What problem are the authors solving? (Abstract and Introduction, pages 1–2)

The paper wants a prefetcher with useful performance and small hardware cost. It starts from offset prefetching because it is much simpler than maintaining a separate learned stream for every address pattern. For each relevant access, it can apply a learned distance `k`.

The authors compare their idea to two earlier offset approaches:

1. **Sandbox Prefetcher (SP):** tries multiple candidate offsets and favors ones whose predictions are accurate. Accuracy alone can reward predictions that arrive too late.
2. **Best-Offset Prefetcher (BOP):** favors a timely offset. However, it uses **one best offset** during prefetching, which can leave other useful offsets unused.

The authors' central claim is that an offset should not simply be classified as good or bad using one timeliness test. Different offsets may cover different misses with different amounts of lead time. MLOP measures that spectrum with multiple lookahead levels.

**Key distinction:** *Coverage* asks “How many misses can I help?” *Timeliness* asks “How far ahead do I make the request?” MLOP explicitly tries to address both.

## 4. The core idea, with a small example

Assume these are successive relevant accesses to blocks in the same tracked region:

`10 → 12 → 14 → 16`

For offset `+2`, block `10` could predict `12` one access before `12` occurs. It could also predict `14` from `12`, and `16` from `14`. Thus `+2` has good **lookahead-1** evidence. In this sequence it has no evidence that the *same `+2` relationship* predicts a target at least three accesses ahead.

Now add another pattern: `20 → 50 → 22`. Offset `+2` relates `20` to `22`, with the access to `50` in between. The first access provides more lead time for `22` than in the consecutive `10 → 12` example. MLOP records evidence for the offset at multiple lookahead thresholds. Its larger-lookahead score reflects cases where the triggering block is sufficiently far back in the access order.

The formal test in the paper is: when current block `A` is accessed, candidate offset `k` could have predicted it if `A − k` was previously accessed in the same access map. For lookahead level `L`, the prior access must be old enough in the recorded access order: MLOP excludes the most recent `L − 1` accesses before applying the test. That makes `L = 1` the least restrictive score and larger levels progressively more restrictive.

**Important nuance:** This is a training test: “Could an earlier access have triggered a prefetch for this block?” It is not proof that an actual memory request would have arrived on time. Access count is only a practical proxy for elapsed time.

## 5. Figure 1: hardware structures (page 2)

Figure 1 shows an **Access Map Table (AMT)** and a score array.

- **AMT entry:** an address or tag identifying a neighborhood of blocks, a **64-bit access map**, and the **ordered last accesses** for that map. A set bit says its corresponding block has been accessed. The map saves storage because nearby blocks share one address/tag rather than storing full addresses separately.
- **Candidate offsets:** distances the prefetcher tests. The paper says it evaluates several offsets, but does **not** give a complete candidate list or range in these four pages.
- **Score array:** for each lookahead level, there is a score for each candidate offset. Figure 1 shows separate rows, such as lookahead 1 and 2. The circled top score in each row identifies that level's best offset. One offset may win more than one level.
- **Evaluation period:** scores are collected for a window of accesses; the authors report that **500 accesses** was a suitable period in their experiments. After evaluation, the best offset from each lookahead level is selected for prefetching. The paper does not fully specify the reset, tie-breaking, or switching details.

Do not confuse the **64 bits in each access map** with the **16 lookahead levels**. They serve different purposes: 64 positions track blocks in a neighborhood; 16 levels track how far ahead an offset has worked.

## 6. How MLOP works, step by step (Section 2, pages 2–3)

### A. Train on observed accesses

For an access to block `A`:

1. Find its AMT entry using high-order address bits. The entry's 64-bit vector says which nearby blocks have been accessed.
2. At lookahead 1, inspect older set bits. For each candidate `k`, if the bit for `A − k` is set, increment `score[1][k]`.
3. At lookahead 2, exclude the immediately preceding access from the map *for this calculation*, then test again. At lookahead 4, exclude the last three accesses. In general, for level `L`, exclude the last `L − 1` accesses. The implementation therefore needs access order, not just a set of visited blocks.
4. After scoring, set the bit for `A` and record it in the ordered recent-access information.

The paper uses **16 lookahead levels**, so it keeps the **last 15 accesses within each bit-vector** to perform the exclusions. It says score updates can be done efficiently using bit-vector shifts, but leaves the circuit details to prior work.

### B. Select offsets

At an evaluation boundary, find the highest-scoring offset **separately for each lookahead level**. MLOP can therefore select several offsets, unlike BOP's single winner. The paper does not fully specify whether identical winners are deduplicated when requests are made.

### C. Issue prefetches in priority order

When generating actual prefetches, start with the winner for **lookahead 1**, then move to larger levels. The idea is to request blocks expected sooner first. This is a priority ordering, **not** a claim that level 1 provides the most time for memory to respond. Larger levels are trained on longer access distances and may have more lead time; the paper prioritizes the nearer anticipated needs.

### D. Hardware size

The authors report a **256-entry AMT per core**, approximately **8.47 KB**. With other metadata, total prefetcher storage is **under 12 KB**. They say this AMT size is close to their best performance point, while even a configuration around **4 KB** retains a significant fraction of its benefit. These are results in their DPC3 setting, not universal sizes for every machine.

## 7. What was compared and how? (Section 3.1, page 3)

The authors use the **DPC3 simulation framework**. They report 46 provided single-core traces, exclude two memory-insensitive traces, and construct 11 multi-core mixes from the remaining 44. They state the mixes are distinct and do not reuse a single-core trace across two mixes. The introduction calls the evaluation a set of **57 workloads**, although the methodology's explicit `44 + 11` count gives **55**; the short paper does not reconcile those counts. Remember this as a reporting ambiguity rather than silently changing either number.

All compared prefetchers are placed at **L1-D** and trained on **L1-D misses**. Metadata tables are enlarged until performance levels off or the DPC3 storage rules would be broken. The compared methods are:

| Method | Main idea described in this paper | Limitation discussed by the authors |
| --- | --- | --- |
| BOP | Test offsets against a small Recent Requests table; use one winning offset. | Other useful offsets can be suppressed; its described update tests one offset at a time. |
| ASP (Aggregate Stride Prefetcher) | Use a history buffer and a single global lookahead, such as eight accesses. | The authors say its shifting map structure loses history and its degree scheme creates many extra predictions. |
| SPP (Signature Path Prefetcher) | Learn signatures of access patterns and adjust prefetch degree. | Its results depend on throttling decisions. |
| MLOP | Score offsets across several lookahead levels and use the winner of each. | More offsets can produce more requests; this paper gives only limited detail on controlling them. |

The paper's comparisons and explanations are the **authors' reported observations**, not independent proof that each competitor always behaves that way on other workloads.

## 8. Reading Figures 2 and 3 (page 4)

**Figure 2: miss coverage.** The vertical axis is the percentage of misses covered; bars compare BOP, ASP, SPP, and MLOP for selected single-core traces. “Avg SC” averages *all* single-core workloads, not only the displayed traces. The paper reports **56% average coverage for MLOP**. ASP is close in coverage, but the authors say it has about **31% more overprediction than MLOP**; that overprediction result is in a footnote and **not plotted**.

**Figure 3: performance improvement.** The vertical axis is improvement relative to a system with **no prefetcher**. Higher bars mean better IPC under the paper's normalization. It shows selected single-core traces, four named mixes, and averages for single-core (**Avg SC**), multi-core (**Avg MC**), and all workloads (**Avg All**). The authors report **30% average improvement** for MLOP and say it is **4% ahead of SPP**, the next best method on their average. One displayed single-core trace has much larger improvements than the axis range; its values are printed above the bars rather than represented by bar height alone. Do not infer every workload improves by 30%.

**What these measures cannot say alone:** High coverage is not automatically high IPC. A prefetch may be late, unnecessary, or expensive in memory bandwidth. IPC is closer to the end goal, but this short paper does not present all underlying latency, bandwidth, accuracy, energy, or per-workload numerical data in tables.

## 9. What is specified, and what you must not assume

This is a short championship paper, not a full implementation manual. It clearly specifies the **conceptual scoring rule**, the **16 lookahead levels**, the **64-bit maps**, the **15 ordered recent accesses per map**, the **256-entry AMT**, the approximate storage, the **500-access** evaluation period, and the **low-to-high lookahead issue priority**.

It does **not** fully specify the candidate-offset set, how offset-score ties are broken, how scores are reset or aged, duplicate-winner handling, prefetch request limits, queue behavior, page-boundary behavior, address translation handling, or every cache-fill and training interaction. Those choices must be recorded as **implementation assumptions** if you build MLOP in gem5. Also, directly adding lookahead tests to gem5's BOP table would not by itself reproduce the paper: MLOP's AMT and ordered per-map history are central to the method.

## 10. Common questions, answered simply

**Is `+4` four bytes?** No. It means four cache blocks forward. If blocks were 64 bytes, the address difference would be 256 bytes; the paper's offset is still `+4`.

**Does lookahead 16 mean 16 cycles?** No. It means an earlier trigger was at least 16 relevant accesses before the target in the training order. The elapsed cycles can vary greatly.

**Why keep recent accesses in order when there is already a bit-vector?** A bit-vector says *what* was seen, but not *when*. To test a larger lookahead, MLOP must remove the newest accesses from consideration.

**Why can one offset win more than one lookahead?** The same pattern can be useful at several lead times. The paper does not state whether duplicate winners issue duplicate requests, so an implementation must decide how to handle that.

**Why issue level 1 first when level 16 is more timely in training?** Level 1 corresponds to a nearer anticipated demand, so the authors prioritize urgency when issuing requests. Level 16 offers more theoretical lead time but its predicted demand is farther away in access order.

**Are 56% and 30% comparable numbers?** No. The first is miss coverage; the second is average performance improvement versus no prefetcher. They have different denominators and meanings.

## 11. A suggested learning path

1. Be able to explain a cache miss, prefetch, cache-block offset, coverage, timeliness, and IPC without reading notes.
2. Draw a trace such as `10, 12, 14, 16`, and identify which `A − k` accesses could predict each later block.
3. Repeat after excluding the last one, two, and three accesses. You now understand lookahead 2, 3, and 4.
4. Draw Figure 1 in your own words: AMT entry → per-lookahead scores → best offset per lookahead → prioritized requests.
5. Explain in two sentences why SP, BOP, and MLOP can choose different offsets.
6. Read Figures 2 and 3 separately: coverage first, performance second. Explain why they need not move together.
7. If implementing, write down each unspecified decision in Section 9 before coding or claiming replication.

## 12. Copy this prompt into ChatGPT

Attach the **original X6 PDF** to ChatGPT along with this prompt. The prompt asks for an interactive tutorial, which is often easier to absorb than a single long answer.

> I attached the four-page paper “Multi-Lookahead Offset Prefetching” (DPC3, 2019) by Shakerinava and coauthors. Teach it to me from beginner level, as if I know basic programming but am new to computer architecture. Read the entire attached PDF, including its footnotes and Figures 1–3, before teaching. Base every paper-specific claim on the PDF; say clearly when you are adding general background, making an inference, or filling an implementation gap. Refer to PDF page numbers and figure numbers. Do not invent values from charts or missing implementation details.
>
> Teach in **short lessons**, one lesson per reply, and wait for me to say “next” before continuing. Start with: (1) memory hierarchy and cache misses; (2) what prefetching does; (3) block addresses and offset prefetching; (4) accuracy versus timeliness versus coverage; (5) SP and BOP; (6) MLOP's central idea; (7) Access Map Table and bit-vector; (8) lookahead scoring with a complete hand-worked trace and a score table for at least three offsets and four lookahead levels; (9) winner selection and issue priority; (10) hardware sizes; (11) evaluation setup and each comparison method; (12) a careful reading of Figures 2 and 3; (13) limitations and unclear implementation details; (14) what a faithful gem5 implementation would need.
>
> For every lesson, use plain language first, then the exact technical terms, then one concrete example. Define every acronym when it first appears. Ask me two comprehension questions at the end and correct my answers gently before moving on. In the worked trace, distinguish block-number offsets from byte addresses and access-count lookahead from elapsed clock cycles. Explain the apparent count mismatch between “57 workloads” and the methodology's 44 included single-core traces plus 11 mixes without pretending the paper resolves it. If you discuss gem5, separate what the paper specifies from design choices I would have to make. Begin with lesson 1 only.

---

**Source note:** This guide paraphrases and explains the attached paper. Worked sequences, intuition, and the learning path are teaching examples added here; they are not experiments reported by the authors.
