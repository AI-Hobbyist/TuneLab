#pragma once

#include "managed_abi.h"
#include <Windows.h>
#include <coreclr_delegates.h>
#include <filesystem>
#include <hostfxr.h>
#include <nethost.h>
#include <string>
#include <vector>

namespace TuneLab::Cubase
{
// Initialized off the audio thread. The CLR and hostfxr stay process-resident;
// document/view handles retain explicit instance ownership.
class ManagedRuntime
{
  public:
    bool initialize(const std::filesystem::path &directory)
    {
        assembly = directory / L"TuneLab.Cubase.Managed.dll";
        auto config = directory / L"TuneLab.Cubase.Managed.runtimeconfig.json";
        HMODULE discoveryLibrary = LoadLibraryW((directory / L"nethost.dll").c_str());
        if (!discoveryLibrary)
        {
            return false;
        }
        auto getHostPath = reinterpret_cast<decltype(&get_hostfxr_path)>(
            GetProcAddress(discoveryLibrary, "get_hostfxr_path"));
        if (!getHostPath)
        {
            FreeLibrary(discoveryLibrary);
            return false;
        }
        get_hostfxr_parameters parameters{sizeof(get_hostfxr_parameters), assembly.c_str(),
                                          nullptr};
        size_t length = 0;
        getHostPath(nullptr, &length, &parameters);
        if (length == 0)
        {
            FreeLibrary(discoveryLibrary);
            return false;
        }
        std::vector<wchar_t> path(length);
        const int discoveryResult = getHostPath(path.data(), &length, &parameters);
        FreeLibrary(discoveryLibrary);
        if (discoveryResult != 0)
        {
            return false;
        }
        HMODULE library = LoadLibraryW(path.data());
        if (!library)
        {
            return false;
        }
        auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
            GetProcAddress(library, "hostfxr_initialize_for_runtime_config"));
        auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
            GetProcAddress(library, "hostfxr_get_runtime_delegate"));
        auto close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(library, "hostfxr_close"));
        if (!initialize || !getDelegate || !close)
        {
            return false;
        }
        hostfxr_handle context = nullptr;
        if (initialize(config.c_str(), nullptr, &context) < 0 || !context)
        {
            return false;
        }
        void *delegate = nullptr;
        const int result =
            getDelegate(context, hdt_load_assembly_and_get_function_pointer, &delegate);
        close(context);
        if (result != 0 || !delegate)
        {
            return false;
        }
        load = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(delegate);
        GetAbiVersion version = nullptr;
        return resolve(L"GetAbiVersion", version) && version() == managedAbiVersion &&
               resolve(L"CreateDocument", createDocument) &&
               resolve(L"DestroyDocument", destroyDocument) && resolve(L"CreateView", createView) &&
               resolve(L"DestroyView", destroyView) && resolve(L"ResizeView", resizeView) &&
               resolve(L"FocusView", focusView) && resolve(L"ScaleView", scaleView) &&
               resolve(L"InitializeServices", initializeServices) &&
               resolve(L"ValidateVoice", validateVoice) &&
               resolve(L"RenderSnapshot", renderSnapshot) &&
               resolve(L"RenderPreview", renderPreview) &&
               resolve(L"GetRenderedAudio", getRenderedAudio) &&
               resolve(L"ReleaseRenderedAudio", releaseRenderedAudio) &&
               resolve(L"CreateRenderRequest", createRenderRequest) &&
               resolve(L"CancelRenderRequest", cancelRenderRequest) &&
               resolve(L"ReleaseRenderRequest", releaseRenderRequest) &&
               resolve(L"ValidateProjectState", validateProjectState) &&
               resolve(L"CopyDocumentState", copyDocumentState) &&
               resolve(L"RestoreDocumentState", restoreDocumentState) &&
               resolve(L"RestoreAraDocumentState", restoreAraDocumentState) &&
               resolve(L"SetDocumentStateCallback", setDocumentStateCallback) &&
               resolve(L"MapDocumentSourcePosition", mapDocumentSourcePosition) &&
               resolve(L"SetDocumentPositionCallback", setDocumentPositionCallback) &&
               resolve(L"SetDocumentStatusCallback", setDocumentStatusCallback) &&
               resolve(L"BuildAraProjectState", buildAraProjectState) &&
               resolve(L"BuildAraRenderState", buildAraRenderState) &&
               resolve(L"SetDocumentKeyPreviewCallback", setDocumentKeyPreviewCallback) &&
               resolve(L"RegisterPairEndpoint", registerPairEndpoint) &&
               resolve(L"PairEndpoints", pairEndpoints) &&
               resolve(L"GetPairEndpointState", getPairEndpointState) &&
               resolve(L"RestorePairIntent", restorePairIntent) &&
               resolve(L"UnpairEndpoint", unpairEndpoint) &&
               resolve(L"ReleasePairEndpoint", releasePairEndpoint) &&
               resolve(L"SetPairOutputCallback", setPairOutputCallback) &&
               resolve(L"SetPairDocumentAccess", setPairDocumentAccess) &&
               resolve(L"CopyPairDocumentState", copyPairDocumentState) &&
               resolve(L"ApplyPairDocumentState", applyPairDocumentState) &&
               resolve(L"SetPairViewProvider", setPairViewProvider) &&
               resolve(L"SetDocumentPairEndpoint", setDocumentPairEndpoint) &&
               resolve(L"CreatePairDocumentProxy", createPairDocumentProxy) &&
               resolve(L"CreatePairView", createPairView) &&
               resolve(L"DestroyPairView", destroyPairView) &&
               resolve(L"ResizePairView", resizePairView) &&
               resolve(L"FocusPairView", focusPairView) && resolve(L"ScalePairView", scalePairView);
    }

    SetPairViewProvider setPairViewProvider = nullptr;
    SetDocumentPairEndpoint setDocumentPairEndpoint = nullptr;
    CreatePairDocumentProxy createPairDocumentProxy = nullptr;
    CreatePairView createPairView = nullptr;
    DestroyPairView destroyPairView = nullptr;
    ResizePairView resizePairView = nullptr;
    FocusPairView focusPairView = nullptr;
    ScalePairView scalePairView = nullptr;
    SetPairDocumentAccess setPairDocumentAccess = nullptr;
    CopyPairDocumentState copyPairDocumentState = nullptr;
    ApplyPairDocumentState applyPairDocumentState = nullptr;
    SetPairOutputCallback setPairOutputCallback = nullptr;
    RegisterPairEndpoint registerPairEndpoint = nullptr;
    PairEndpoints pairEndpoints = nullptr;
    GetPairEndpointState getPairEndpointState = nullptr;
    RestorePairIntent restorePairIntent = nullptr;
    UnpairEndpoint unpairEndpoint = nullptr;
    ReleasePairEndpoint releasePairEndpoint = nullptr;
    CreateDocument createDocument = nullptr;
    DestroyDocument destroyDocument = nullptr;
    CreateView createView = nullptr;
    DestroyView destroyView = nullptr;
    ResizeView resizeView = nullptr;
    FocusView focusView = nullptr;
    ScaleView scaleView = nullptr;
    InitializeServices initializeServices = nullptr;
    ValidateVoice validateVoice = nullptr;
    RenderSnapshot renderSnapshot = nullptr;
    RenderPreview renderPreview = nullptr;
    GetRenderedAudio getRenderedAudio = nullptr;
    ReleaseRenderedAudio releaseRenderedAudio = nullptr;
    CreateRenderRequest createRenderRequest = nullptr;
    CancelRenderRequest cancelRenderRequest = nullptr;
    ReleaseRenderRequest releaseRenderRequest = nullptr;
    ValidateProjectState validateProjectState = nullptr;
    CopyDocumentState copyDocumentState = nullptr;
    RestoreDocumentState restoreDocumentState = nullptr;
    RestoreDocumentState restoreAraDocumentState = nullptr;
    SetDocumentStateCallback setDocumentStateCallback = nullptr;
    MapDocumentSourcePosition mapDocumentSourcePosition = nullptr;
    SetDocumentPositionCallback setDocumentPositionCallback = nullptr;
    SetDocumentStatusCallback setDocumentStatusCallback = nullptr;
    BuildAraProjectState buildAraProjectState = nullptr;
    BuildAraRenderState buildAraRenderState = nullptr;
    SetDocumentKeyPreviewCallback setDocumentKeyPreviewCallback = nullptr;

  private:
    template <typename Function> bool resolve(const wchar_t *method, Function &function)
    {
        void *entry = nullptr;
        const int result =
            load(assembly.c_str(), L"TuneLab.Cubase.Managed.NativeEntry, TuneLab.Cubase.Managed",
                 method, UNMANAGEDCALLERSONLY_METHOD, nullptr, &entry);
        function = reinterpret_cast<Function>(entry);
        return result == 0 && function;
    }

    std::filesystem::path assembly;
    load_assembly_and_get_function_pointer_fn load = nullptr;
};
} // namespace TuneLab::Cubase
