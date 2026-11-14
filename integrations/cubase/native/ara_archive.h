#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace TuneLab::Cubase
{
struct AraModificationRecord
{
    std::string persistentId;
    std::vector<std::uint8_t> state;
};

inline constexpr std::array<std::uint8_t, 8> araArchiveMagic{'T', 'L', 'A', 'R',
                                                             'A', '0', '0', '1'};
inline constexpr std::array<std::uint8_t, 8> araProbeArchive{'T', 'L', 'P', 'R', 'O', 'B', 'E', 1};

inline void appendArchiveInteger(std::vector<std::uint8_t> &bytes, std::size_t value)
{
    if (value > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
        throw std::invalid_argument("ARA archive field exceeds the supported size");
    for (int index = 0; index < 4; ++index)
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
}

inline std::vector<std::uint8_t> encodeAraArchive(const std::vector<AraModificationRecord> &records)
{
    std::vector<std::uint8_t> bytes(araArchiveMagic.begin(), araArchiveMagic.end());
    appendArchiveInteger(bytes, records.size());
    std::set<std::string> ids;
    for (const auto &record : records)
    {
        if (record.persistentId.empty() || record.persistentId.find('\0') != std::string::npos ||
            !ids.insert(record.persistentId).second)
            throw std::invalid_argument("Invalid or duplicate ARA modification ID");
        appendArchiveInteger(bytes, record.persistentId.size());
        appendArchiveInteger(bytes, record.state.size());
        bytes.insert(bytes.end(), record.persistentId.begin(), record.persistentId.end());
        bytes.insert(bytes.end(), record.state.begin(), record.state.end());
        if (bytes.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
            throw std::invalid_argument("ARA archive exceeds the supported size");
    }
    return bytes;
}

inline std::vector<AraModificationRecord> decodeAraArchive(const std::vector<std::uint8_t> &bytes)
{
    if (bytes.size() == araProbeArchive.size() &&
        std::equal(bytes.begin(), bytes.end(), araProbeArchive.begin()))
        return {};
    if (bytes.size() < 12 ||
        !std::equal(araArchiveMagic.begin(), araArchiveMagic.end(), bytes.begin()))
        throw std::invalid_argument("Invalid ARA archive header or version");
    std::size_t cursor = 8;
    const auto readInteger = [&]() -> std::uint32_t {
        if (bytes.size() - cursor < 4)
            throw std::invalid_argument("Truncated ARA archive field");
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
            value |= std::uint32_t(bytes[cursor++]) << (index * 8);
        return value;
    };
    const auto count = readInteger();
    if (count > (bytes.size() - cursor) / 8)
        throw std::invalid_argument("Invalid ARA archive record count");
    std::vector<AraModificationRecord> records;
    std::set<std::string> ids;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto idSize = readInteger();
        const auto stateSize = readInteger();
        if (idSize == 0 || idSize > bytes.size() - cursor)
            throw std::invalid_argument("Invalid ARA archive ID length");
        AraModificationRecord record;
        record.persistentId.assign(reinterpret_cast<const char *>(bytes.data() + cursor), idSize);
        cursor += idSize;
        if (record.persistentId.find('\0') != std::string::npos ||
            !ids.insert(record.persistentId).second || stateSize > bytes.size() - cursor)
            throw std::invalid_argument("Invalid ARA archive record");
        record.state.assign(bytes.begin() + cursor, bytes.begin() + cursor + stateSize);
        cursor += stateSize;
        records.push_back(std::move(record));
    }
    if (cursor != bytes.size())
        throw std::invalid_argument("Trailing ARA archive bytes");
    return records;
}
} // namespace TuneLab::Cubase
