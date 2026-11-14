#include "editor_view.h"
#include "managed_runtime.h"
#include <iostream>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: tunelab_runtime_probe <managed-directory>\n";
        return 1;
    }
    TuneLab::Cubase::ManagedRuntime runtime;
    if (!runtime.initialize(argv[1]))
    {
        std::cerr << "Managed runtime discovery or entry resolution failed\n";
        return 2;
    }
    TuneLab::Cubase::DocumentHandle document = 0;
    if (runtime.createDocument(&document) != 0 || document == 0)
    {
        return 3;
    }
    if (runtime.destroyDocument(document) != 0)
    {
        return 4;
    }
    std::cout << "Runtime discovered; document/view entry points resolved; document released\n";
    return 0;
}
