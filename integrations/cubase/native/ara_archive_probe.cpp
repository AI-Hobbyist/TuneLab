#include "ara_archive.h"
#include <iostream>

using namespace TuneLab::Cubase;

template <typename Action> bool rejects(Action action)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument &)
    {
        return true;
    }
    return false;
}

int main()
{
    const std::vector<AraModificationRecord> original{{"mod-1", {1, 2, 3}}, {"mod-2", {4, 5}}};
    auto bytes = encodeAraArchive(original);
    auto restored = decodeAraArchive(bytes);
    if (restored.size() != 2 || restored[0].persistentId != "mod-1" ||
        restored[0].state != original[0].state || restored[1].state != original[1].state)
        return 1;
    restored[0].state[0] = 99;
    if (original[0].state[0] != 1 || restored[1].state[0] != 4)
        return 2;
    if (!decodeAraArchive(encodeAraArchive({})).empty() ||
        !decodeAraArchive({araProbeArchive.begin(), araProbeArchive.end()}).empty())
        return 3;
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        if (!rejects([&] { decodeAraArchive({bytes.begin(), bytes.begin() + length}); }))
            return 4;
    }
    auto unknown = bytes;
    unknown[7] = '2';
    if (!rejects([&] { decodeAraArchive(unknown); }))
        return 5;
    bytes.push_back(0);
    if (!rejects([&] { decodeAraArchive(bytes); }))
        return 6;
    if (!rejects([] { encodeAraArchive({{"same", {}}, {"same", {}}}); }) ||
        !rejects([] { encodeAraArchive({{"", {}}}); }))
        return 7;
    auto duplicate = encodeAraArchive({{"a", {}}, {"b", {}}});
    duplicate.back() = 'a';
    if (!rejects([&] { decodeAraArchive(duplicate); }))
        return 8;
    std::cout << "ARA archive codec: modification records, byte isolation, legacy marker, all "
                 "truncations, versions and duplicate IDs passed\n";
    return 0;
}
