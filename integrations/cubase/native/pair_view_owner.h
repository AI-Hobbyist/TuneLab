#pragma once

#include "editor_view.h"
#include "pair_endpoint.h"

namespace TuneLab::Cubase
{
// All operations use the owner host/UI thread. Registration and every created
// view retain one reference so removal of the ARA object cannot free a document
// still displayed by a companion plugin. No C++ object crosses the C ABI.
class PairViewOwner
{
  public:
    static int registerWith(PairEndpoint &endpoint, std::shared_ptr<EditorDocument> document)
    {
        auto *owner = new PairViewOwner(std::move(document));
        const auto result =
            endpoint.setViewProvider(create, destroy, resize, focus, releaseRegistration, owner);
        if (result != 0)
            delete owner;
        return result;
    }

  private:
    explicit PairViewOwner(std::shared_ptr<EditorDocument> document) : document(std::move(document))
    {
    }
    void release()
    {
        if (--references == 0)
            delete this;
    }
    static std::int32_t __cdecl create(void *value, void *parent, std::int32_t width,
                                       std::int32_t height, std::uint64_t *view) noexcept
    {
        try
        {
            auto &owner = *static_cast<PairViewOwner *>(value);
            if (GetCurrentThreadId() != owner.hostThread)
                return static_cast<std::int32_t>(BridgeResult::WrongThread);
            if (!owner.document->initialize())
                return static_cast<std::int32_t>(BridgeResult::Failed);
            const auto result = owner.document->runtime.createView(owner.document->handle, parent,
                                                                   width, height, view);
            if (result == 0)
                ++owner.references;
            return result;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return static_cast<std::int32_t>(BridgeResult::Failed);
        }
    }
    static std::int32_t __cdecl destroy(void *value, std::uint64_t view) noexcept
    {
        auto &owner = *static_cast<PairViewOwner *>(value);
        if (GetCurrentThreadId() != owner.hostThread)
            return static_cast<std::int32_t>(BridgeResult::WrongThread);
        const auto result = owner.document->runtime.destroyView(view);
        if (result == 0)
            owner.release();
        return result;
    }
    static std::int32_t __cdecl resize(void *value, std::uint64_t view, std::int32_t width,
                                       std::int32_t height) noexcept
    {
        auto &owner = *static_cast<PairViewOwner *>(value);
        if (GetCurrentThreadId() != owner.hostThread)
            return static_cast<std::int32_t>(BridgeResult::WrongThread);
        return owner.document->runtime.resizeView(view, width, height);
    }
    static std::int32_t __cdecl focus(void *value, std::uint64_t view) noexcept
    {
        auto &owner = *static_cast<PairViewOwner *>(value);
        if (GetCurrentThreadId() != owner.hostThread)
            return static_cast<std::int32_t>(BridgeResult::WrongThread);
        return owner.document->runtime.focusView(view);
    }
    static void __cdecl releaseRegistration(void *value) noexcept
    {
        static_cast<PairViewOwner *>(value)->release();
    }
    std::shared_ptr<EditorDocument> document;
    const DWORD hostThread = GetCurrentThreadId();
    int references = 1;
};
} // namespace TuneLab::Cubase
