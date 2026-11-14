#pragma once

#include <cstdint>

// Private host integration ABI. This does not change TuneLab.SDK/Foundation.
namespace TuneLab::Cubase
{
inline constexpr std::uint32_t managedAbiVersion = 1;

enum class BridgeResult : std::int32_t
{
    Success = 0,
    InvalidArgument = 1,
    InvalidHandle = 2,
    UnsupportedVersion = 3,
    WrongThread = 4,
    Failed = 5
};

// Handles are owned by the managed instance registry. Zero is always invalid;
// native code must never dereference a handle or release it with delete/free.
using DocumentHandle = std::uint64_t;
using ViewHandle = std::uint64_t;
using GetAbiVersion = std::int32_t(__cdecl *)();
using CreateDocument = std::int32_t(__cdecl *)(DocumentHandle *);
using DestroyDocument = std::int32_t(__cdecl *)(DocumentHandle);
using CreateView = std::int32_t(__cdecl *)(DocumentHandle, void *, std::int32_t, std::int32_t,
                                           ViewHandle *);
using DestroyView = std::int32_t(__cdecl *)(ViewHandle);
using ResizeView = std::int32_t(__cdecl *)(ViewHandle, std::int32_t, std::int32_t);
using FocusView = std::int32_t(__cdecl *)(ViewHandle);
} // namespace TuneLab::Cubase
