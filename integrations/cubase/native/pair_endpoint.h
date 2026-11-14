#pragma once

#include "managed_runtime.h"
#include <array>
#include <atomic>

namespace TuneLab::Cubase
{
// Instance-owned control-thread resource. Only the published bool is read from
// process(); deregistration serializes with callbacks before destroying owner.
class PairEndpoint
{
  public:
    PairEndpoint() = default;
    PairEndpoint(const PairEndpoint &) = delete;
    PairEndpoint &operator=(const PairEndpoint &) = delete;
    ~PairEndpoint()
    {
        reset();
    }

    bool initialize(const ManagedRuntime &nextRuntime, int role)
    {
        if (registered)
            return true;
        runtime = nextRuntime;
        controlThread = GetCurrentThreadId();
        if (runtime.registerPairEndpoint(role, endpoint.data()) != 0)
            return false;
        registered = true;
        if (runtime.setPairOutputCallback(endpoint.data(), onOutputChanged, this) != 0)
        {
            reset();
            return false;
        }
        return true;
    }
    void reset() noexcept
    {
        if (!registered)
            return;
        stateChanged = nullptr;
        stateOwner = nullptr;
        runtime.setPairOutputCallback(endpoint.data(), nullptr, nullptr);
        runtime.releasePairEndpoint(endpoint.data());
        registered = false;
        endpoint.fill(0);
        projectOutput.store(true);
        statePending.store(false);
    }
    using StateChanged = bool (*)(void *) noexcept;

    // Install and drain only on the endpoint's control thread. Foreign-thread
    // registry notifications publish the atomic flags without touching the UI.
    void setStateChangedCallback(StateChanged callback, void *owner) noexcept
    {
        controlThread = GetCurrentThreadId();
        stateChanged = callback;
        stateOwner = owner;
    }
    void publishPendingState() noexcept
    {
        if (GetCurrentThreadId() != controlThread.load() || !stateChanged ||
            !statePending.exchange(false))
            return;
        if (!stateChanged(stateOwner))
            statePending.store(true);
    }
    const std::array<std::uint8_t, 16> &id() const noexcept
    {
        return endpoint;
    }
    bool allowsProjectOutput() const noexcept
    {
        return projectOutput.load();
    }
    int setViewProvider(PairCreateViewCallback create, PairDestroyViewCallback destroy,
                        PairResizeViewCallback resize, PairFocusViewCallback focus,
                        PairReleaseViewProvider release, PairScaleViewCallback scale, void *owner)
    {
        return runtime.setPairViewProvider(endpoint.data(), create, destroy, resize, focus, release,
                                           scale, owner);
    }
    int setDocumentAccess(PairCopyDocumentCallback copy, PairApplyDocumentCallback apply,
                          void *owner)
    {
        return runtime.setPairDocumentAccess(endpoint.data(), copy, apply, owner);
    }
    int copyDocumentState(std::uint8_t *destination, std::int32_t capacity, std::int32_t *required)
    {
        return runtime.copyPairDocumentState(endpoint.data(), destination, capacity, required);
    }
    int applyDocumentState(const std::uint8_t *state, std::int32_t length)
    {
        return runtime.applyPairDocumentState(endpoint.data(), state, length);
    }
    int pairWith(const std::uint8_t *bridge)
    {
        return runtime.pairEndpoints(endpoint.data(), bridge);
    }
    int restoreIntent(bool requiresPair)
    {
        return runtime.restorePairIntent(endpoint.data(), requiresPair ? 1 : 0);
    }
    int unpair()
    {
        return runtime.unpairEndpoint(endpoint.data());
    }
    int state(std::array<std::uint8_t, 16> &owner, std::int32_t &status, std::int32_t &output)
    {
        return runtime.getPairEndpointState(endpoint.data(), owner.data(), &status, &output);
    }

  private:
    static void __cdecl onOutputChanged(void *owner, std::int32_t output) noexcept
    {
        auto &endpoint = *static_cast<PairEndpoint *>(owner);
        endpoint.projectOutput.store(output != 0);
        endpoint.statePending.store(true);
        endpoint.publishPendingState();
    }
    ManagedRuntime runtime;
    std::array<std::uint8_t, 16> endpoint{};
    std::atomic<bool> projectOutput{true};
    std::atomic<bool> statePending{false};
    std::atomic<DWORD> controlThread{0};
    StateChanged stateChanged = nullptr;
    void *stateOwner = nullptr;
    bool registered = false;
};
} // namespace TuneLab::Cubase
