#include "editor_view.h"
#include "managed_runtime.h"
#include <iostream>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2 && ((argc != 3 && argc != 5) || std::wcscmp(argv[2], L"--services") != 0))
    {
        std::cerr << "Usage: tunelab_runtime_probe <managed-directory> [--services]\n";
        return 1;
    }
    TuneLab::Cubase::ManagedRuntime runtime;
    if (!runtime.initialize(argv[1]))
    {
        std::cerr << "Managed runtime discovery or entry resolution failed\n";
        return 2;
    }
    if (argc >= 3)
    {
        if (runtime.initializeServices() != 0 || runtime.initializeServices() != 0)
        {
            std::cerr << "Shared engine services initialization failed\n";
            return 5;
        }
        std::cout << "Shared engine services initialized twice without teardown\n";
        if (argc == 5 &&
            runtime.validateVoice(argv[3], static_cast<std::int32_t>(std::wcslen(argv[3])), argv[4],
                                  static_cast<std::int32_t>(std::wcslen(argv[4]))) != 0)
        {
            std::cerr << "Requested voice is unavailable through the integration loader\n";
            return 6;
        }
        if (argc == 5)
        {
            std::cout << "Requested voice is available through the integration loader\n";
        }
    }
    TuneLab::Cubase::DocumentHandle document = 0;
    if (runtime.createDocument(&document) != 0 || document == 0)
    {
        return 3;
    }
    std::int32_t required = 0;
    if (runtime.copyDocumentState(document, nullptr, 0, &required) != 0 || required <= 16)
    {
        return 7;
    }
    std::vector<std::uint8_t> state(required);
    if (runtime.copyDocumentState(document, state.data(), required, &required) != 0 ||
        runtime.validateProjectState(state.data(), required) != 0 ||
        runtime.restoreDocumentState(document, state.data(), required) != 0)
    {
        return 8;
    }
    auto invalid = state;
    invalid[16] = 0xFF;
    if (runtime.restoreDocumentState(document, invalid.data(), required) == 0)
    {
        return 9;
    }
    std::vector<std::uint8_t> restored(required);
    if (runtime.copyDocumentState(document, restored.data(), required, &required) != 0 ||
        restored != state)
    {
        return 10;
    }
    std::cout << "Document state copy, deferred restore and invalid-state preservation passed\n";
    if (runtime.destroyDocument(document) != 0)
    {
        return 4;
    }
    std::cout << "Runtime discovered; document/view entry points resolved; document released\n";
    return 0;
}
