#pragma once

#include <cstdint>

// Private host integration ABI. This does not change TuneLab.SDK/Foundation.
namespace TuneLab::Cubase
{
inline constexpr std::uint32_t managedAbiVersion = 16;

struct AraTempoInput
{
    double seconds;
    double quarter;
};
struct AraMeterInput
{
    double quarter;
    std::int32_t numerator;
    std::int32_t denominator;
};
struct AraNoteInput
{
    std::uint64_t id;
    double seconds;
    double duration;
    std::int32_t pitch;
};
struct AraRegionInput
{
    double sourceStart;
    double sourceDuration;
    double songStart;
    double songDuration;
};
static_assert(sizeof(AraTempoInput) == 16 && sizeof(AraMeterInput) == 16 &&
              sizeof(AraNoteInput) == 32 && sizeof(AraRegionInput) == 32);
using BuildAraRenderState = std::int32_t(__cdecl *)(const std::uint8_t *, std::int32_t, double,
                                                    std::uint8_t *, std::int32_t, std::int32_t *);
using BuildAraProjectState = std::int32_t(__cdecl *)(const std::uint8_t *, std::int32_t,
                                                     const AraTempoInput *, std::int32_t,
                                                     const AraMeterInput *, std::int32_t,
                                                     const AraNoteInput *, std::int32_t,
                                                     const AraRegionInput *, std::uint8_t *,
                                                     std::int32_t, std::int32_t *);

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
// Pairing endpoint/owner buffers contain exactly 16 GUID bytes, control thread only.
using RegisterPairEndpoint = std::int32_t(__cdecl *)(std::int32_t, std::uint8_t *);
using PairOutputCallback = void(__cdecl *)(void *, std::int32_t);
using SetPairOutputCallback = std::int32_t(__cdecl *)(const std::uint8_t *, PairOutputCallback,
                                                      void *);
using PairCopyDocumentCallback = std::int32_t(__cdecl *)(void *, std::uint8_t *, std::int32_t,
                                                         std::int32_t *);
using PairApplyDocumentCallback = std::int32_t(__cdecl *)(void *, const std::uint8_t *,
                                                          std::int32_t);
using SetPairDocumentAccess = std::int32_t(__cdecl *)(const std::uint8_t *,
                                                      PairCopyDocumentCallback,
                                                      PairApplyDocumentCallback, void *);
using CopyPairDocumentState = std::int32_t(__cdecl *)(const std::uint8_t *, std::uint8_t *,
                                                      std::int32_t, std::int32_t *);
using ApplyPairDocumentState = std::int32_t(__cdecl *)(const std::uint8_t *, const std::uint8_t *,
                                                       std::int32_t);
using PairCreateViewCallback = std::int32_t(__cdecl *)(void *, void *, std::int32_t, std::int32_t,
                                                       std::uint64_t *);
using PairDestroyViewCallback = std::int32_t(__cdecl *)(void *, std::uint64_t);
using PairResizeViewCallback = std::int32_t(__cdecl *)(void *, std::uint64_t, std::int32_t,
                                                       std::int32_t);
using PairFocusViewCallback = std::int32_t(__cdecl *)(void *, std::uint64_t);
using PairScaleViewCallback = std::int32_t(__cdecl *)(void *, std::uint64_t, double);
using PairReleaseViewProvider = void(__cdecl *)(void *);
using SetPairViewProvider = std::int32_t(__cdecl *)(const std::uint8_t *, PairCreateViewCallback,
                                                    PairDestroyViewCallback, PairResizeViewCallback,
                                                    PairFocusViewCallback, PairReleaseViewProvider,
                                                    PairScaleViewCallback, void *);
using SetDocumentPairEndpoint = std::int32_t(__cdecl *)(DocumentHandle, const std::uint8_t *);
using CreatePairDocumentProxy = std::int32_t(__cdecl *)(const std::uint8_t *, DocumentHandle *);
using CreatePairView = std::int32_t(__cdecl *)(const std::uint8_t *, void *, std::int32_t,
                                               std::int32_t, std::uint8_t *);
using DestroyPairView = std::int32_t(__cdecl *)(const std::uint8_t *);
using ResizePairView = std::int32_t(__cdecl *)(const std::uint8_t *, std::int32_t, std::int32_t);
using FocusPairView = std::int32_t(__cdecl *)(const std::uint8_t *);
using ScalePairView = std::int32_t(__cdecl *)(const std::uint8_t *, double);
using PairEndpoints = std::int32_t(__cdecl *)(const std::uint8_t *, const std::uint8_t *);
using GetPairEndpointState = std::int32_t(__cdecl *)(const std::uint8_t *, std::uint8_t *,
                                                     std::int32_t *, std::int32_t *);
using RestorePairIntent = std::int32_t(__cdecl *)(const std::uint8_t *, std::int32_t);
using UnpairEndpoint = std::int32_t(__cdecl *)(const std::uint8_t *);
using ReleasePairEndpoint = std::int32_t(__cdecl *)(const std::uint8_t *);
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
using ScaleView = std::int32_t(__cdecl *)(ViewHandle, double);
using DocumentStateCallback = void(__cdecl *)(void *, const std::uint8_t *, std::int32_t);
using SetDocumentStateCallback = std::int32_t(__cdecl *)(DocumentHandle, DocumentStateCallback,
                                                         void *);
using DocumentPositionCallback = double(__cdecl *)(void *);
using DocumentStatusCallback = std::int32_t(__cdecl *)(void *, std::int32_t *, std::int32_t *,
                                                       std::int64_t *);
using SetDocumentStatusCallback = std::int32_t(__cdecl *)(DocumentHandle, DocumentStatusCallback,
                                                          void *);
using DocumentKeyPreviewCallback = void(__cdecl *)(void *, std::int32_t, std::int32_t);
using SetDocumentKeyPreviewCallback = std::int32_t(__cdecl *)(DocumentHandle,
                                                              DocumentKeyPreviewCallback, void *);
using SetDocumentPositionCallback = std::int32_t(__cdecl *)(DocumentHandle,
                                                            DocumentPositionCallback, void *);
using MapDocumentSourcePosition = std::int32_t(__cdecl *)(DocumentHandle, double, double *);
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
