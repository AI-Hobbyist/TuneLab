#pragma once

#include "managed_runtime.h"
#include "public.sdk/source/common/pluginview.h"
#include <cstring>
#include <functional>
#include <limits>
#include <memory>

namespace TuneLab::Cubase
{
// Shared with the controller so closing a view leaves its document alive.
struct EditorDocument
{
    std::filesystem::path managedDirectory;
    ManagedRuntime runtime;
    DocumentHandle handle = 0;
    std::vector<std::uint8_t> state;
    std::function<void(const std::vector<std::uint8_t> &)> changed;
    std::function<double()> pollPosition;
    std::function<std::int32_t(std::int32_t *, std::int32_t *, std::int64_t *)> pollStatus;
    bool callbackRegistered = false;

    bool initialize()
    {
        if (handle != 0)
        {
            return true;
        }
        if (!runtime.initialize(managedDirectory) || runtime.createDocument(&handle) != 0 ||
            handle == 0)
        {
            return false;
        }
        callbackRegistered = runtime.setDocumentStateCallback(handle, onStateChanged, this) == 0;
        if (!callbackRegistered ||
            runtime.setDocumentPositionCallback(handle, onPositionRequested, this) != 0 ||
            runtime.setDocumentStatusCallback(handle, onStatusRequested, this) != 0 ||
            (!state.empty() &&
             runtime.restoreDocumentState(handle, state.data(),
                                          static_cast<std::int32_t>(state.size())) != 0))
        {
            runtime.setDocumentStateCallback(handle, nullptr, nullptr);
            runtime.destroyDocument(handle);
            handle = 0;
            callbackRegistered = false;
            return false;
        }
        return true;
    }

    static std::int32_t __cdecl onStatusRequested(void *owner, std::int32_t *song,
                                                  std::int32_t *preview,
                                                  std::int64_t *elapsed) noexcept
    {
        try
        {
            auto &document = *static_cast<EditorDocument *>(owner);
            return document.pollStatus ? document.pollStatus(song, preview, elapsed) : 2;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return 5;
        }
    }

    static double __cdecl onPositionRequested(void *owner) noexcept
    {
        try
        {
            auto &document = *static_cast<EditorDocument *>(owner);
            return document.pollPosition ? document.pollPosition()
                                         : std::numeric_limits<double>::quiet_NaN();
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    static void __cdecl onStateChanged(void *owner, const std::uint8_t *bytes,
                                       std::int32_t length) noexcept
    {
        auto &document = *static_cast<EditorDocument *>(owner);
        try
        {
            std::vector<std::uint8_t> snapshot(bytes, bytes + length);
            if (snapshot == document.state)
            {
                return;
            }
            document.state = std::move(snapshot);
            if (document.changed)
            {
                document.changed(document.state);
            }
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
        }
    }

    ~EditorDocument()
    {
        if (handle != 0)
        {
            if (callbackRegistered)
            {
                runtime.setDocumentStateCallback(handle, nullptr, nullptr);
            }
            if (runtime.destroyDocument(handle) != 0)
            {
                OutputDebugStringA("TuneLab: managed document destruction failed\n");
            }
        }
    }
};

class EditorView final : public Steinberg::CPluginView
{
  public:
    explicit EditorView(std::shared_ptr<EditorDocument> document) : document(std::move(document))
    {
        rect = Steinberg::ViewRect(0, 0, 1100, 700);
    }

    ~EditorView() override
    {
        if (removed() != Steinberg::kResultOk)
        {
            OutputDebugStringA("TuneLab: managed view removal failed during destruction\n");
        }
    }

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override
    {
        return type && std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API attached(void *parent, Steinberg::FIDString type) override
    {
        if (!parent || view != 0 || isPlatformTypeSupported(type) != Steinberg::kResultTrue)
        {
            return Steinberg::kInvalidArgument;
        }
        if (!document->initialize())
        {
            return Steinberg::kResultFalse;
        }
        if (document->runtime.createView(document->handle, parent, rect.getWidth(),
                                         rect.getHeight(), &view) != 0)
        {
            return Steinberg::kResultFalse;
        }
        return CPluginView::attached(parent, type);
    }

    Steinberg::tresult PLUGIN_API removed() override
    {
        if (view != 0)
        {
            if (document->runtime.destroyView(view) != 0)
            {
                return Steinberg::kResultFalse;
            }
            view = 0;
        }
        return CPluginView::removed();
    }

    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect *size) override
    {
        if (!size || size->getWidth() <= 0 || size->getHeight() <= 0)
        {
            return Steinberg::kInvalidArgument;
        }
        if (view != 0 &&
            document->runtime.resizeView(view, size->getWidth(), size->getHeight()) != 0)
        {
            return Steinberg::kResultFalse;
        }
        return CPluginView::onSize(size);
    }

    Steinberg::tresult PLUGIN_API canResize() override
    {
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool state) override
    {
        // Windows handles focus loss; do not steal focus from another editor.
        if (!state || view == 0)
        {
            return Steinberg::kResultFalse;
        }
        return document->runtime.focusView(view) == 0 ? Steinberg::kResultTrue
                                                      : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect *size) override
    {
        return size && size->getWidth() > 0 && size->getHeight() > 0 ? Steinberg::kResultTrue
                                                                     : Steinberg::kResultFalse;
    }

  private:
    std::shared_ptr<EditorDocument> document;
    ViewHandle view = 0;
};
} // namespace TuneLab::Cubase
