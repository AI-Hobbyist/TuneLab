#pragma once

#include "ara_note_identity.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace TuneLab::Cubase
{
struct AraSourceIdentity
{
    std::uint64_t nextId = 0;
    std::vector<ARA::ARAContentNote> notes;
    std::vector<std::uint64_t> ids;
};

namespace SourceIdentityWire
{
inline constexpr std::array<std::uint8_t, 8> magic{'T', 'L', 'A', 'S', 'R', 'C', '0', '1'};

template <typename T> void append(std::vector<std::uint8_t> &bytes, T value)
{
    static_assert(sizeof(T) == 4 || sizeof(T) == 8);
    using Bits = std::conditional_t<sizeof(T) == 8, std::uint64_t, std::uint32_t>;
    Bits bits;
    std::memcpy(&bits, &value, sizeof(T));
    for (std::size_t index = 0; index < sizeof(T); ++index)
        bytes.push_back(static_cast<std::uint8_t>(bits >> (index * 8)));
}

template <typename T> T read(const std::vector<std::uint8_t> &bytes, std::size_t &cursor)
{
    static_assert(sizeof(T) == 4 || sizeof(T) == 8);
    if (bytes.size() - cursor < sizeof(T))
        throw std::invalid_argument("Truncated ARA source identity field");
    using Bits = std::conditional_t<sizeof(T) == 8, std::uint64_t, std::uint32_t>;
    Bits bits = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index)
        bits |= Bits(bytes[cursor++]) << (index * 8);
    T value;
    std::memcpy(&value, &bits, sizeof(T));
    return value;
}

inline void validate(const AraSourceIdentity &source)
{
    if (source.ids.size() != source.notes.size())
        throw std::invalid_argument("ARA source identity count mismatch");
    std::set<std::uint64_t> seen;
    double previous = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < source.notes.size(); ++index)
    {
        const auto &note = source.notes[index];
        if (source.ids[index] == 0 || source.ids[index] > source.nextId ||
            !seen.insert(source.ids[index]).second || !std::isfinite(note.startPosition) ||
            note.startPosition < previous || !std::isfinite(note.noteDuration) ||
            note.noteDuration < 0 || !std::isfinite(note.attackDuration) ||
            !std::isfinite(note.signalDuration) || note.signalDuration < 0 ||
            !std::isfinite(note.frequency) || note.frequency < 0 || !std::isfinite(note.volume) ||
            note.volume < 0 || note.volume > 1)
            throw std::invalid_argument("Invalid ARA source note identity snapshot");
        previous = note.startPosition;
    }
}
} // namespace SourceIdentityWire

inline std::vector<std::uint8_t> encodeAraSourceIdentity(const AraSourceIdentity &source)
{
    SourceIdentityWire::validate(source);
    if (source.notes.size() > ((std::numeric_limits<std::int32_t>::max)() - 24) / 52)
        throw std::invalid_argument("ARA source identity snapshot is too large");
    std::vector<std::uint8_t> bytes(SourceIdentityWire::magic.begin(),
                                    SourceIdentityWire::magic.end());
    SourceIdentityWire::append(bytes, araNoteMatchingVersion);
    SourceIdentityWire::append(bytes, source.nextId);
    SourceIdentityWire::append(bytes, static_cast<std::uint32_t>(source.notes.size()));
    for (std::size_t index = 0; index < source.notes.size(); ++index)
    {
        const auto &note = source.notes[index];
        SourceIdentityWire::append(bytes, source.ids[index]);
        SourceIdentityWire::append(bytes, note.frequency);
        SourceIdentityWire::append(bytes, note.pitchNumber);
        SourceIdentityWire::append(bytes, note.volume);
        SourceIdentityWire::append(bytes, note.startPosition);
        SourceIdentityWire::append(bytes, note.attackDuration);
        SourceIdentityWire::append(bytes, note.noteDuration);
        SourceIdentityWire::append(bytes, note.signalDuration);
    }
    return bytes;
}

inline AraSourceIdentity decodeAraSourceIdentity(const std::vector<std::uint8_t> &bytes)
{
    if (bytes.size() < 24 || !std::equal(SourceIdentityWire::magic.begin(),
                                         SourceIdentityWire::magic.end(), bytes.begin()))
        throw std::invalid_argument("Invalid ARA source identity header");
    std::size_t cursor = 8;
    if (SourceIdentityWire::read<std::uint32_t>(bytes, cursor) != araNoteMatchingVersion)
        throw std::invalid_argument("Unsupported ARA note matching version");
    AraSourceIdentity source;
    source.nextId = SourceIdentityWire::read<std::uint64_t>(bytes, cursor);
    const auto count = SourceIdentityWire::read<std::uint32_t>(bytes, cursor);
    if (count != (bytes.size() - cursor) / 52 || (bytes.size() - cursor) % 52 != 0)
        throw std::invalid_argument("Invalid ARA source identity record length");
    for (std::uint32_t index = 0; index < count; ++index)
    {
        source.ids.push_back(SourceIdentityWire::read<std::uint64_t>(bytes, cursor));
        ARA::ARAContentNote note{};
        note.frequency = SourceIdentityWire::read<float>(bytes, cursor);
        note.pitchNumber = SourceIdentityWire::read<ARA::ARAPitchNumber>(bytes, cursor);
        note.volume = SourceIdentityWire::read<float>(bytes, cursor);
        note.startPosition = SourceIdentityWire::read<double>(bytes, cursor);
        note.attackDuration = SourceIdentityWire::read<double>(bytes, cursor);
        note.noteDuration = SourceIdentityWire::read<double>(bytes, cursor);
        note.signalDuration = SourceIdentityWire::read<double>(bytes, cursor);
        source.notes.push_back(note);
    }
    SourceIdentityWire::validate(source);
    return source;
}

inline AraSourceIdentity restoreAraSourceIdentity(const AraSourceIdentity &saved,
                                                  const AraSourceIdentity &current)
{
    SourceIdentityWire::validate(saved);
    SourceIdentityWire::validate(current);
    AraSourceIdentity restored = current;
    const auto initialCounter = (std::max)(saved.nextId, current.nextId);
    auto temporaryCounter = initialCounter;
    restored.ids = matchAraNoteIds(saved.notes, saved.ids, current.notes, temporaryCounter);
    const std::set<std::uint64_t> savedIds(saved.ids.begin(), saved.ids.end());
    restored.nextId = initialCounter;
    for (std::size_t index = 0; index < restored.ids.size(); ++index)
    {
        // A post-archive event already has an identity outside the saved
        // allocator domain. Keep it when restoring the same archive again.
        if (!savedIds.count(restored.ids[index]) && current.ids[index] > saved.nextId)
            restored.ids[index] = current.ids[index];
        restored.nextId = (std::max)(restored.nextId, restored.ids[index]);
    }
    return restored;
}
} // namespace TuneLab::Cubase
