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

#ifndef __MEM_CACHE_PREFETCH_MLOP_HH__
#define __MEM_CACHE_PREFETCH_MLOP_HH__

#include <array>
#include <cstdint>
#include <vector>

#include "base/statistics.hh"
#include "mem/cache/prefetch/queued.hh"

namespace gem5
{

struct MLOPPrefetcherParams;

namespace prefetch
{

/** Multi-Lookahead Offset Prefetcher (DPC3 2019). */
class MLOP : public Queued
{
  private:
    static constexpr unsigned MapBlocks = 64;
    static constexpr unsigned Levels = 16;
    static constexpr unsigned RecentAccesses = Levels - 1;

    struct MapEntry
    {
        bool valid = false;
        bool secure = false;
        Addr region = 0;
        uint64_t seen = 0;
        // Positions that occurred before the last 15 accesses. This extra
        // mask makes repeated-position lookahead tests unambiguous.
        uint64_t aged = 0;
        std::array<uint8_t, RecentAccesses> recent{};
        unsigned recentCount = 0;
    };

    const unsigned amtEntries;
    const unsigned evaluationPeriod;
    const unsigned maxOffset;
    const unsigned maxPrefetches;

    std::vector<MapEntry> maps;
    std::vector<int> offsets;
    std::vector<std::array<uint16_t, Levels>> scores;
    std::array<int, Levels> winners;
    unsigned accessesInWindow = 0;

    struct MLOPStats : public statistics::Group
    {
        MLOPStats(statistics::Group *parent);
        statistics::Scalar trainingAccesses;
        statistics::Scalar evaluationWindows;
        statistics::Scalar amtReplacements;
        statistics::Scalar candidates;
        statistics::Scalar duplicateCandidates;
    } statsMLOP;

    void train(MapEntry &entry, unsigned position);
    void selectWinners();

  public:
    MLOP(const MLOPPrefetcherParams &p);

    void notify(const CacheAccessProbeArg &acc,
                const PrefetchInfo &pfi) override;

    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_MLOP_HH__
