#pragma once

#include "managed_runtime.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/common/pluginview.h"
#include <algorithm>
#include <array>
#include <cmath>
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
    std::function<void(std::int32_t, bool)> keyPreview;
    std::function<void()> retryPendingHostUpdate;
    std::function<std::int32_t(std::int32_t *, std::int32_t *, std::int64_t *)> pollStatus;
    bool callbackRegistered = false;
    bool sourcePosition = false;
    std::array<std::uint8_t, 16> pairEndpoint{};

    bool setPairEndpoint(const std::array<std::uint8_t, 16> &endpoint)
    {
        if (handle != 0 && runtime.setDocumentPairEndpoint(handle, endpoint.data()) != 0)
            return false;
        pairEndpoint = endpoint;
        return true;
    }

    // Registration and delivery use the embedded editor/host main thread.
    // Weak observers let several ARA renderers share a modification document.
    void observeStateChanges(const std::shared_ptr<std::function<void()>> &observer)
    {
        stateObservers.erase(std::remove_if(stateObservers.begin(), stateObservers.end(),
                                            [](const auto &entry) { return entry.expired(); }),
                             stateObservers.end());
        for (const auto &entry : stateObservers)
            if (entry.lock() == observer)
                return;
        stateObservers.push_back(observer);
    }

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
            (pairEndpoint != std::array<std::uint8_t, 16>{} &&
             runtime.setDocumentPairEndpoint(handle, pairEndpoint.data()) != 0) ||
            runtime.setDocumentKeyPreviewCallback(handle, onKeyPreviewRequested, this) != 0 ||
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

    static void __cdecl onKeyPreviewRequested(void *owner, std::int32_t pitch,
                                              std::int32_t pressed) noexcept
    {
        try
        {
            auto &document = *static_cast<EditorDocument *>(owner);
            if (document.keyPreview)
                document.keyPreview(pitch, pressed != 0);
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
        }
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
            // Copy before invoking: a successful retry clears its registration.
            auto retry = document.retryPendingHostUpdate;
            if (retry)
                retry();
            double position = document.pollPosition ? document.pollPosition()
                                                    : std::numeric_limits<double>::quiet_NaN();
            if (!document.sourcePosition || !std::isfinite(position))
                return position;
            double quarter = std::numeric_limits<double>::quiet_NaN();
            return document.runtime.mapDocumentSourcePosition(document.handle, position,
                                                              &quarter) == 0
                       ? quarter
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
            // No notification on identical snapshots or suppressed restores.
            // Copy the weak list because a renderer may register while rebuilding.
            const auto observers = document.stateObservers;
            for (const auto &entry : observers)
                if (auto observer = entry.lock())
                    (*observer)();
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
        }
    }

    std::vector<std::weak_ptr<std::function<void()>>> stateObservers;

    ~EditorDocument()
    {
        if (handle != 0)
        {
            if (callbackRegistered)
            {
                runtime.setDocumentStateCallback(handle, nullptr, nullptr);
                runtime.setDocumentKeyPreviewCallback(handle, nullptr, nullptr);
            }
            if (runtime.destroyDocument(handle) != 0)
            {
                OutputDebugStringA("TuneLab: managed document destruction failed\n");
            }
        }
    }
};

class EditorView final : public Steinberg::CPluginView,
                         public Steinberg::IPlugViewContentScaleSupport
{
  public:
    struct Reference
    {
        EditorView *view;
    };
    std::weak_ptr<Reference> reference() const
    {
        return lifetime;
    }
    std::weak_ptr<std::function<void()>> attachmentChanged;

    explicit EditorView(std::shared_ptr<EditorDocument> document) : document(std::move(document))
    {
        rect = Steinberg::ViewRect(0, 0, 1100, 700);
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override
    {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugViewContentScaleSupport::iid))
        {
            if (!object)
                return Steinberg::kInvalidArgument;
            *object = static_cast<Steinberg::IPlugViewContentScaleSupport *>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        return CPluginView::queryInterface(iid, object);
    }
    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return CPluginView::addRef();
    }
    Steinberg::uint32 PLUGIN_API release() override
    {
        return CPluginView::release();
    }

    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override
    {
        if (!std::isfinite(factor) || factor <= 0)
            return Steinberg::kInvalidArgument;
        const double ratio = factor / contentScale;
        const double width = std::round(rect.getWidth() * ratio);
        const double height = std::round(rect.getHeight() * ratio);
        if (width < 1 || height < 1 || width > (std::numeric_limits<Steinberg::int32>::max)() ||
            height > (std::numeric_limits<Steinberg::int32>::max)())
            return Steinberg::kInvalidArgument;
        Steinberg::ViewRect next(0, 0, static_cast<Steinberg::int32>(width),
                                 static_cast<Steinberg::int32>(height));
        const auto previous = rect;
        if (plugFrame && plugFrame->resizeView(this, &next) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;
        if (view != 0 && document->runtime.scaleView(view, factor) != 0)
        {
            if (plugFrame)
            {
                auto restore = previous;
                if (plugFrame->resizeView(this, &restore) != Steinberg::kResultOk)
                    OutputDebugStringA("TuneLab: host rejected scaling rollback\n");
            }
            return Steinberg::kResultFalse;
        }
        contentScale = factor;
        scaleRequested = true;
        rect = next;
        return Steinberg::kResultTrue;
    }

    ~EditorView() override
    {
        lifetime->view = nullptr;
        if (removed() != Steinberg::kResultOk)
        {
            OutputDebugStringA("TuneLab: managed view removal failed during destruction\n");
        }
    }

    Steinberg::tresult setDocument(std::shared_ptr<EditorDocument> next)
    {
        if (next == document)
            return Steinberg::kResultOk;
        if (!isAttached())
        {
            document = std::move(next);
            return Steinberg::kResultOk;
        }
        ViewHandle replacement = 0;
        if (next && (!next->initialize() ||
                     next->runtime.createView(next->handle, systemWindow, rect.getWidth(),
                                              rect.getHeight(), &replacement) != 0))
            return Steinberg::kResultFalse;
        if (replacement != 0 && scaleRequested &&
            next->runtime.scaleView(replacement, contentScale) != 0)
        {
            if (next->runtime.destroyView(replacement) != 0)
                OutputDebugStringA("TuneLab: scaled replacement view cleanup failed\n");
            return Steinberg::kResultFalse;
        }
        if (view != 0 && document->runtime.destroyView(view) != 0)
        {
            if (replacement != 0 && next->runtime.destroyView(replacement) != 0)
                OutputDebugStringA("TuneLab: replacement view cleanup failed\n");
            return Steinberg::kResultFalse;
        }
        document = std::move(next);
        view = replacement;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override
    {
        return type && std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API attached(void *parent, Steinberg::FIDString type) override
    {
        if (!parent || isAttached() || view != 0 ||
            isPlatformTypeSupported(type) != Steinberg::kResultTrue)
        {
            return Steinberg::kInvalidArgument;
        }
        if (!document)
        {
            const auto result = CPluginView::attached(parent, type);
            notifyAttachment();
            return result;
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
        if (scaleRequested && document->runtime.scaleView(view, contentScale) != 0)
        {
            if (document->runtime.destroyView(view) == 0)
                view = 0;
            return Steinberg::kResultFalse;
        }
        const auto result = CPluginView::attached(parent, type);
        notifyAttachment();
        return result;
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
        const bool wasAttached = isAttached();
        const auto result = CPluginView::removed();
        if (wasAttached)
            notifyAttachment();
        return result;
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
    void notifyAttachment()
    {
        if (auto callback = attachmentChanged.lock(); callback && *callback)
            (*callback)();
    }
    std::shared_ptr<Reference> lifetime = std::make_shared<Reference>(Reference{this});
    std::shared_ptr<EditorDocument> document;
    ViewHandle view = 0;
    double contentScale = 1;
    bool scaleRequested = false;
};
} // namespace TuneLab::Cubase
