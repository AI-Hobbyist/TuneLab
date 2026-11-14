#include "editor_view.h"
#include <iostream>

namespace
{
int created = 0;
int removed = 0;
int documentsReleased = 0;
bool rejectRemoval = true;

std::int32_t __cdecl createView(TuneLab::Cubase::DocumentHandle, void *, std::int32_t, std::int32_t,
                                TuneLab::Cubase::ViewHandle *handle)
{
    *handle = ++created;
    return 0;
}

std::int32_t __cdecl destroyView(TuneLab::Cubase::ViewHandle)
{
    if (rejectRemoval)
    {
        return 4;
    }
    ++removed;
    return 0;
}

std::int32_t __cdecl destroyDocument(TuneLab::Cubase::DocumentHandle)
{
    ++documentsReleased;
    return 0;
}
} // namespace

// Exercises IPlugView ownership and protocol only. No HWND is created and the
// managed callbacks are test doubles; this is not GUI or Cubase validation.
int main()
{
    using namespace Steinberg;
    auto document = std::make_shared<TuneLab::Cubase::EditorDocument>();
    document->handle = 1;
    document->runtime.createView = createView;
    document->runtime.destroyView = destroyView;
    document->runtime.destroyDocument = destroyDocument;
    document->runtime.resizeView = [](auto, auto, auto) -> std::int32_t { return 0; };
    document->runtime.focusView = [](auto) -> std::int32_t { return 0; };
    auto *view = new TuneLab::Cubase::EditorView(document);
    IPlugView *api = view;
    if (api->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue ||
        api->isPlatformTypeSupported("unsupported") != kResultFalse ||
        api->attached(nullptr, kPlatformTypeHWND) != kInvalidArgument || created != 0)
    {
        return 1;
    }
    if (api->attached(reinterpret_cast<void *>(1), kPlatformTypeHWND) != kResultOk || created != 1)
    {
        return 2;
    }
    ViewRect size(0, 0, 900, 600);
    ViewRect invalid(0, 0, 0, 600);
    if (api->onSize(&invalid) != kInvalidArgument || api->onSize(&size) != kResultTrue ||
        api->onFocus(true) != kResultTrue || api->onFocus(false) != kResultFalse)
    {
        return 3;
    }
    if (api->removed() != kResultFalse || !view->isAttached() || documentsReleased != 0)
    {
        return 4;
    }
    rejectRemoval = false;
    if (api->removed() != kResultOk || api->removed() != kResultOk || removed != 1)
    {
        return 5;
    }
    api->release();
    if (documentsReleased != 0)
    {
        return 6;
    }
    document.reset();
    if (documentsReleased != 1)
    {
        return 7;
    }
    std::cout
        << "View protocol ownership, removal retry, resize and focus dispatch passed (no GUI)\n";
    return 0;
}
