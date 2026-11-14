#include "editor_binding.h"
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
    {
        TuneLab::Cubase::EditorDocument observed;
        int firstCalls = 0, secondCalls = 0;
        auto first = std::make_shared<std::function<void()>>([&] { ++firstCalls; });
        auto second = std::make_shared<std::function<void()>>([&] { ++secondCalls; });
        observed.observeStateChanges(first);
        observed.observeStateChanges(first);
        observed.observeStateChanges(second);
        const std::uint8_t initial[]{1};
        TuneLab::Cubase::EditorDocument::onStateChanged(&observed, initial, 1);
        TuneLab::Cubase::EditorDocument::onStateChanged(&observed, initial, 1);
        if (firstCalls != 1 || secondCalls != 1)
            return 15;
        first.reset();
        const std::uint8_t next[]{2};
        TuneLab::Cubase::EditorDocument::onStateChanged(&observed, next, 1);
        if (firstCalls != 1 || secondCalls != 2)
            return 16;
        observed.observeStateChanges(second);
        if (observed.stateObservers.size() != 1)
            return 17;
    }

    int previewPitch = -1;
    bool previewPressed = false;
    document->keyPreview = [&](std::int32_t pitch, bool pressed) {
        previewPitch = pitch;
        previewPressed = pressed;
    };
    TuneLab::Cubase::EditorDocument::onKeyPreviewRequested(document.get(), 64, 1);
    if (previewPitch != 64 || !previewPressed)
        return 18;
    TuneLab::Cubase::EditorDocument::onKeyPreviewRequested(document.get(), -1, 0);
    if (previewPitch != -1 || previewPressed)
        return 19;
    document->keyPreview = {};
    TuneLab::Cubase::EditorDocument::onKeyPreviewRequested(document.get(), 67, 1);
    if (previewPitch != -1)
        return 20;
    std::int64_t expiredToken = 0;
    {
        TuneLab::Cubase::EditorBinding first;
        TuneLab::Cubase::EditorBinding second;
        first.select(document);
        std::shared_ptr<TuneLab::Cubase::EditorDocument> resolved;
        if (first.id() == second.id() ||
            !TuneLab::Cubase::EditorBinding::resolve(first.id(), resolved) ||
            resolved != document ||
            !TuneLab::Cubase::EditorBinding::resolve(second.id(), resolved) || resolved)
            return 13;
        expiredToken = first.id();
    }
    std::shared_ptr<TuneLab::Cubase::EditorDocument> resolved;
    if (TuneLab::Cubase::EditorBinding::resolve(expiredToken, resolved))
        return 14;
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
    auto next = std::make_shared<TuneLab::Cubase::EditorDocument>();
    next->handle = 2;
    next->runtime.createView = createView;
    next->runtime.destroyView = destroyView;
    next->runtime.destroyDocument = destroyDocument;
    next->runtime.resizeView = document->runtime.resizeView;
    next->runtime.focusView = document->runtime.focusView;
    next->runtime.createView = [](auto, auto, auto, auto, auto *) -> std::int32_t { return 5; };
    if (view->setDocument(next) != kResultFalse || created != 1 || removed != 0 ||
        !view->isAttached() || api->onFocus(true) != kResultTrue)
        return 12;
    next->runtime.createView = createView;
    if (view->setDocument(next) != kResultOk || created != 2 || removed != 1 || !view->isAttached())
        return 8;
    if (view->setDocument(nullptr) != kResultOk || removed != 2 || !view->isAttached() ||
        api->onFocus(true) != kResultFalse)
        return 9;
    if (view->setDocument(document) != kResultOk || created != 3)
        return 10;
    if (api->removed() != kResultOk || api->removed() != kResultOk || removed != 3)
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
    next.reset();
    if (documentsReleased != 2)
        return 11;
    std::cout
        << "View protocol ownership, removal retry, resize and focus dispatch passed (no GUI)\n";
    return 0;
}
