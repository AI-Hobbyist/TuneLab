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
               resolve(L"FocusView", focusView);
    }

    CreateDocument createDocument = nullptr;
    DestroyDocument destroyDocument = nullptr;
    CreateView createView = nullptr;
    DestroyView destroyView = nullptr;
    ResizeView resizeView = nullptr;
    FocusView focusView = nullptr;

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
