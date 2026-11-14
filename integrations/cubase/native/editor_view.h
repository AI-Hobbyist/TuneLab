#pragma once

#include "managed_runtime.h"
#include "public.sdk/source/common/pluginview.h"
#include <cstring>
#include <memory>

namespace TuneLab::Cubase
{
// Shared with the controller so closing a view leaves its document alive.
struct EditorDocument
{
    std::filesystem::path managedDirectory;
    ManagedRuntime runtime;
    DocumentHandle handle = 0;

    bool initialize()
    {
        return handle != 0 || (runtime.initialize(managedDirectory) &&
                               runtime.createDocument(&handle) == 0 && handle != 0);
    }

    ~EditorDocument()
    {
        if (handle != 0)
        {
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
