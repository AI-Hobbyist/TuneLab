#pragma once

#include <cstdint>

// Private host integration ABI. This does not change TuneLab.SDK/Foundation.
namespace TuneLab::Cubase
{
inline constexpr std::uint32_t managedAbiVersion = 2;

enum class BridgeResult : std::int32_t
{
    Success = 0,
    InvalidArgument = 1,
    InvalidHandle = 2,
    UnsupportedVersion = 3,
    WrongThread = 4,
    Failed = 5,
    Cancelled = 6,
    Busy = 7
};

// Handles are owned by the managed instance registry. Zero is always invalid;
// native code must never dereference a handle or release it with delete/free.
using DocumentHandle = std::uint64_t;
using ViewHandle = std::uint64_t;
using GetAbiVersion = std::int32_t(__cdecl *)();
using InitializeServices = std::int32_t(__cdecl *)();
using ValidateVoice = std::int32_t(__cdecl *)(const wchar_t *, std::int32_t, const wchar_t *,
                                              std::int32_t);
using CreateDocument = std::int32_t(__cdecl *)(DocumentHandle *);
using DestroyDocument = std::int32_t(__cdecl *)(DocumentHandle);
using CreateView = std::int32_t(__cdecl *)(DocumentHandle, void *, std::int32_t, std::int32_t,
                                           ViewHandle *);
using DestroyView = std::int32_t(__cdecl *)(ViewHandle);
using ResizeView = std::int32_t(__cdecl *)(ViewHandle, std::int32_t, std::int32_t);
using FocusView = std::int32_t(__cdecl *)(ViewHandle);
using DocumentStateCallback = void(__cdecl *)(void *, const std::uint8_t *, std::int32_t);
using SetDocumentStateCallback = std::int32_t(__cdecl *)(DocumentHandle, DocumentStateCallback,
                                                         void *);
using DocumentPositionCallback = double(__cdecl *)(void *);
using DocumentStatusCallback = std::int32_t(__cdecl *)(void *, std::int32_t *, std::int32_t *,
                                                       std::int64_t *);
using SetDocumentStatusCallback = std::int32_t(__cdecl *)(DocumentHandle, DocumentStatusCallback,
                                                          void *);
using SetDocumentPositionCallback = std::int32_t(__cdecl *)(DocumentHandle,
                                                            DocumentPositionCallback, void *);
using ValidateProjectState = std::int32_t(__cdecl *)(const std::uint8_t *, std::int32_t);
using CopyDocumentState = std::int32_t(__cdecl *)(DocumentHandle, std::uint8_t *, std::int32_t,
                                                  std::int32_t *);
using RestoreDocumentState = std::int32_t(__cdecl *)(DocumentHandle, const std::uint8_t *,
                                                     std::int32_t);
using AudioHandle = std::uint64_t;
using RenderRequestHandle = std::uint64_t;
using CreateRenderRequest = std::int32_t(__cdecl *)(RenderRequestHandle *);
using CancelRenderRequest = std::int32_t(__cdecl *)(RenderRequestHandle);
using ReleaseRenderRequest = std::int32_t(__cdecl *)(RenderRequestHandle);
using RenderSnapshot = std::int32_t(__cdecl *)(const std::uint8_t *, std::int32_t, std::int32_t,
                                               RenderRequestHandle, AudioHandle *);
using RenderPreview = std::int32_t(__cdecl *)(const std::uint8_t *, std::int32_t, std::int32_t,
                                              std::int32_t, RenderRequestHandle, AudioHandle *);
using GetRenderedAudio = std::int32_t(__cdecl *)(AudioHandle, std::int32_t *, std::int32_t *,
                                                 const float **);
using ReleaseRenderedAudio = std::int32_t(__cdecl *)(AudioHandle);
} // namespace TuneLab::Cubase
