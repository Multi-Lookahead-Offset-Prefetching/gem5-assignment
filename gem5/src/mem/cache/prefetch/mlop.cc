/*
 * Copyright (c) 2026 MLOP assignment contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/cache/prefetch/mlop.hh"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>

#include "base/intmath.hh"
#include "base/logging.hh"
#include "params/MLOPPrefetcher.hh"

namespace gem5
{
namespace prefetch
{

MLOP::MLOPStats::MLOPStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(trainingAccesses, statistics::units::Count::get(),
               "L1 data misses used to train MLOP"),
      ADD_STAT(evaluationWindows, statistics::units::Count::get(),
               "completed offset scoring windows"),
      ADD_STAT(amtReplacements, statistics::units::Count::get(),
               "direct-mapped access map replacements"),
      ADD_STAT(candidates, statistics::units::Count::get(),
               "unique MLOP prefetch candidates"),
      ADD_STAT(duplicateCandidates, statistics::units::Count::get(),
               "duplicate winners suppressed on the same access")
{}

MLOP::MLOP(const MLOPPrefetcherParams &p)
    : Queued(p),
      amtEntries(p.amt_entries),
      evaluationPeriod(p.evaluation_period),
      maxOffset(p.max_offset),
      maxPrefetches(p.max_prefetches),
      maps(amtEntries),
      statsMLOP(this)
{
    fatal_if(!isPowerOf2(blkSize),
             "%s: cache block size must be a power of two", name());
    fatal_if(!isPowerOf2(amtEntries), "%s: AMT size must be a power of two",
             name());
    fatal_if(evaluationPeriod == 0 ||
                 evaluationPeriod > std::numeric_limits<uint16_t>::max(),
             "%s: evaluation period must be in 1..65535", name());
    fatal_if(maxOffset == 0 || maxOffset >= MapBlocks,
             "%s: max offset must be in 1..63 blocks", name());
    fatal_if(maxPrefetches == 0 || maxPrefetches > Levels,
             "%s: max prefetches must be in 1..16", name());

    // Small offsets win ties, then the positive offset. Both directions
    // are candidates; the paper does not prescribe a complete offset list.
    for (unsigned distance = 1; distance <= maxOffset; ++distance) {
        offsets.push_back(distance);
        offsets.push_back(-static_cast<int>(distance));
    }
    scores.resize(offsets.size());
    winners.fill(0);
}

void
MLOP::notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi)
{
    // A prefetch-generated miss must not train another prefetch.
    if (!acc.pkt->cmd.isHWPrefetch()) {
        Queued::notify(acc, pfi);
    }
}

void
MLOP::train(MapEntry &entry, unsigned position)
{
    const uint64_t current_bit = uint64_t{1} << position;

    // Test the current access against predecessors in the same map.
    // The oldest matching predecessor gives the greatest valid lookahead.
    // 'aged' records predecessors older than the 15-position history, so
    // repeated accesses to one block are handled correctly.
    for (size_t index = 0; index < offsets.size(); ++index) {
        const int predecessor = static_cast<int>(position) - offsets[index];
        if (predecessor < 0 || predecessor >= MapBlocks) {
            continue;
        }
        const uint64_t bit = uint64_t{1} << predecessor;
        if (!(entry.seen & bit)) {
            continue;
        }

        unsigned lead = 0;
        if (entry.aged & bit) {
            lead = Levels;
        } else {
            for (unsigned i = 0; i < entry.recentCount; ++i) {
                if (entry.recent[i] == predecessor) {
                    lead = entry.recentCount - i;
                    break;
                }
            }
        }
        for (unsigned level = 0; level < lead; ++level) {
            ++scores[index][level];
        }
    }

    if (entry.recentCount == RecentAccesses) {
        entry.aged |= uint64_t{1} << entry.recent[0];
        std::move(entry.recent.begin() + 1, entry.recent.end(),
                  entry.recent.begin());
        entry.recent[RecentAccesses - 1] = position;
    } else {
        entry.recent[entry.recentCount++] = position;
    }
    entry.seen |= current_bit;
}

void
MLOP::selectWinners()
{
    for (unsigned level = 0; level < Levels; ++level) {
        uint16_t best_score = 0;
        winners[level] = 0;
        for (size_t i = 0; i < offsets.size(); ++i) {
            if (scores[i][level] > best_score) {
                best_score = scores[i][level];
                winners[level] = offsets[i];
            }
            scores[i][level] = 0;
        }
    }
    accessesInWindow = 0;
    statsMLOP.evaluationWindows++;
}

void
MLOP::calculatePrefetch(const PrefetchInfo &pfi,
                        std::vector<AddrPriority> &addresses,
                        const CacheAccessor &)
{
    const Addr block = blockIndex(pfi.getAddr());
    const Addr region = block / MapBlocks;
    const unsigned position = block % MapBlocks;
    MapEntry &entry = maps[region & (amtEntries - 1)];

    if (!entry.valid || entry.region != region ||
        entry.secure != pfi.isSecure()) {
        if (entry.valid) {
            statsMLOP.amtReplacements++;
        }
        entry = MapEntry{};
        entry.valid = true;
        entry.secure = pfi.isSecure();
        entry.region = region;
    }

    train(entry, position);
    statsMLOP.trainingAccesses++;
    if (++accessesInWindow == evaluationPeriod) {
        selectWinners();
    }

    std::set<Addr> emitted;
    const Addr last_block = std::numeric_limits<Addr>::max() / blkSize;
    for (unsigned level = 0; level < Levels; ++level) {
        const int offset = winners[level];
        if (offset == 0) {
            continue;
        }
        const Addr distance = std::abs(offset);
        if ((offset < 0 && block < distance) ||
            (offset > 0 && block > last_block - distance)) {
            continue;
        }
        const Addr target_block =
            offset > 0 ? block + distance : block - distance;
        const Addr target = target_block * blkSize;
        if (!samePage(target, pfi.getAddr())) {
            continue;
        }
        if (!emitted.insert(target).second) {
            statsMLOP.duplicateCandidates++;
            continue;
        }
        // gem5's queue favors larger priority values. Level 1 is first.
        addresses.emplace_back(target, Levels - level);
        statsMLOP.candidates++;
        if (addresses.size() == maxPrefetches) {
            break;
        }
    }
}

} // namespace prefetch
} // namespace gem5
