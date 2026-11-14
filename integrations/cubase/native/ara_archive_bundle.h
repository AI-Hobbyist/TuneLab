#pragma once

#include "ara_archive.h"
#include "ara_source_identity.h"

namespace TuneLab::Cubase
{
struct AraArchiveBundle
{
    std::vector<AraModificationRecord> modifications;
    std::vector<AraModificationRecord> sources;
};
inline constexpr std::array<std::uint8_t, 8> araBundleMagic{'T', 'L', 'A', 'R', 'A', '0', '0', '2'};

inline std::vector<std::uint8_t> encodeAraArchiveBundle(const AraArchiveBundle &bundle)
{
    const auto modifications = encodeAraArchive(bundle.modifications);
    for (const auto &source : bundle.sources)
        decodeAraSourceIdentity(source.state);
    const auto sources = encodeAraArchive(bundle.sources);
    std::vector<std::uint8_t> bytes(araBundleMagic.begin(), araBundleMagic.end());
    appendArchiveInteger(bytes, modifications.size());
    appendArchiveInteger(bytes, sources.size());
    bytes.insert(bytes.end(), modifications.begin(), modifications.end());
    bytes.insert(bytes.end(), sources.begin(), sources.end());
    if (bytes.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
        throw std::invalid_argument("ARA archive bundle exceeds the supported size");
    return bytes;
}

inline AraArchiveBundle decodeAraArchiveBundle(const std::vector<std::uint8_t> &bytes)
{
    if (bytes.size() < 8 ||
        !std::equal(araBundleMagic.begin(), araBundleMagic.end(), bytes.begin()))
        return {decodeAraArchive(bytes), {}};
    if (bytes.size() < 16)
        throw std::invalid_argument("Truncated ARA archive bundle");
    std::size_t cursor = 8;
    const auto readSize = [&]() {
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
            value |= std::uint32_t(bytes[cursor++]) << (index * 8);
        return value;
    };
    const auto modificationSize = readSize();
    const auto sourceSize = readSize();
    if (modificationSize > bytes.size() - cursor ||
        sourceSize != bytes.size() - cursor - modificationSize)
        throw std::invalid_argument("Invalid ARA archive bundle length");
    AraArchiveBundle bundle;
    bundle.modifications =
        decodeAraArchive({bytes.begin() + cursor, bytes.begin() + cursor + modificationSize});
    cursor += modificationSize;
    bundle.sources = decodeAraArchive({bytes.begin() + cursor, bytes.end()});
    for (const auto &source : bundle.sources)
        decodeAraSourceIdentity(source.state);
    return bundle;
}
} // namespace TuneLab::Cubase
