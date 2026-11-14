#include <Windows.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <iostream>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4)
    {
        std::wcerr
            << L"Usage: tunelab_host_probe <hostfxr.dll> <runtimeconfig.json> <assembly.dll>\n";
        return 1;
    }

    // The CLR remains process-wide; only the temporary initialization context is closed.
    HMODULE library = LoadLibraryW(argv[1]);
    if (!library)
    {
        std::cerr << "Cannot load hostfxr\n";
        return 2;
    }

    auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        GetProcAddress(library, "hostfxr_initialize_for_runtime_config"));
    auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        GetProcAddress(library, "hostfxr_get_runtime_delegate"));
    auto close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(library, "hostfxr_close"));
    if (!initialize || !getDelegate || !close)
    {
        std::cerr << "Missing hostfxr entry points\n";
        return 3;
    }

    hostfxr_handle context = nullptr;
    int result = initialize(argv[2], nullptr, &context);
    if (result < 0 || !context)
    {
        std::cerr << "Runtime initialization failed: " << result << '\n';
        return 4;
    }

    void *delegate = nullptr;
    result = getDelegate(context, hdt_load_assembly_and_get_function_pointer, &delegate);
    close(context);
    if (result != 0 || !delegate)
    {
        std::cerr << "Runtime delegate unavailable: " << result << '\n';
        return 5;
    }

    auto load = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(delegate);
    void *entry = nullptr;
    result = load(argv[3], L"TuneLab.Cubase.Managed.NativeEntry, TuneLab.Cubase.Managed",
                  L"GetAbiVersion", UNMANAGEDCALLERSONLY_METHOD, nullptr, &entry);
    if (result != 0 || !entry)
    {
        std::cerr << "Managed entry unavailable: " << result << '\n';
        return 6;
    }

    auto getVersion = reinterpret_cast<int(__cdecl *)()>(entry);
    const int version = getVersion();
    std::cout << "Managed ABI version: " << version << '\n';
    return version == 1 ? 0 : 7;
}
