#pragma once

#include "ARA_API/ARAInterface.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace TuneLab::Cubase
{
inline constexpr std::uint32_t araNoteMatchingVersion = 1;

inline int araNoteDifferenceGroups(const ARA::ARAContentNote &left,
                                   const ARA::ARAContentNote &right)
{
    return int(left.startPosition != right.startPosition) +
           int(left.pitchNumber != right.pitchNumber || left.frequency != right.frequency) +
           int(left.noteDuration != right.noteDuration ||
               left.signalDuration != right.signalDuration) +
           int(left.attackDuration != right.attackDuration) + int(left.volume != right.volume);
}

// Identity is local to a source persistent ID. Exact matches consume separate
// occurrences, including identical duplicates. Changed events inherit identity
// only for an unambiguous one-to-one change of one semantic field group.
inline std::vector<std::uint64_t> matchAraNoteIds(const std::vector<ARA::ARAContentNote> &previous,
                                                  const std::vector<std::uint64_t> &previousIds,
                                                  const std::vector<ARA::ARAContentNote> &current,
                                                  std::uint64_t &nextId)
{
    std::vector<std::uint64_t> ids(current.size());
    std::vector<bool> consumed(previous.size());
    for (std::size_t next = 0; next < current.size(); ++next)
        for (std::size_t old = 0; old < previous.size(); ++old)
            if (!consumed[old] && araNoteDifferenceGroups(previous[old], current[next]) == 0)
            {
                ids[next] = previousIds[old];
                consumed[old] = true;
                break;
            }

    // Determine ambiguity before assigning any changed event, so iteration
    // order cannot turn an ambiguous pair into an apparently unique match.
    std::vector<std::size_t> candidate(current.size(), previous.size());
    std::vector<std::size_t> oldCounts(previous.size());
    std::vector<std::size_t> newCounts(current.size());
    for (std::size_t next = 0; next < current.size(); ++next)
        if (ids[next] == 0)
            for (std::size_t old = 0; old < previous.size(); ++old)
                if (!consumed[old] && araNoteDifferenceGroups(previous[old], current[next]) == 1)
                {
                    candidate[next] = old;
                    ++oldCounts[old];
                    ++newCounts[next];
                }
    for (std::size_t next = 0; next < current.size(); ++next)
    {
        if (ids[next] != 0)
            continue;
        const auto old = candidate[next];
        if (newCounts[next] == 1 && oldCounts[old] == 1)
            ids[next] = previousIds[old];
        else
        {
            if (nextId == (std::numeric_limits<std::uint64_t>::max)())
                throw std::overflow_error("ARA source note identity space is exhausted");
            ids[next] = ++nextId;
        }
    }
    return ids;
}
} // namespace TuneLab::Cubase
