#include "editor_controller.h"
#include "managed_runtime.h"
#include "pair_view_owner.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <array>
#include <atomic>
#include <iostream>
#include <thread>

namespace
{
struct ViewProbeState
{
    int created = 0, destroyed = 0, documentsReleased = 0, resized = 0, focused = 0;
    bool rejectDestroy = true;
    double scale = 0;
    TuneLab::Cubase::DocumentHandle document = 0;
} viewProbe;

// Registry notifications may arrive on a foreign control thread. Only the
// registering thread delivers UI messages; failed delivery remains retryable.
bool testPairNotifications(TuneLab::Cubase::ManagedRuntime &runtime,
                           TuneLab::Cubase::ManagedRuntime &peer)
{
    using namespace TuneLab::Cubase;
    PairEndpoint instrument, bridge;
    if (!instrument.initialize(runtime, 1) || !bridge.initialize(peer, 2))
        return false;
    struct Observer
    {
        PairEndpoint *endpoint;
        DWORD thread;
        int calls = 0;
        std::int32_t status = -1;
        bool reject = false;
        bool wrongThread = false;
    } observer{&instrument, GetCurrentThreadId()};
    instrument.setStateChangedCallback(
        [](void *owner) noexcept {
            auto &observer = *static_cast<Observer *>(owner);
            observer.wrongThread = observer.wrongThread || GetCurrentThreadId() != observer.thread;
            ++observer.calls;
            std::array<std::uint8_t, 16> ownerId{};
            std::int32_t output = -1;
            return observer.endpoint->state(ownerId, observer.status, output) == 0 &&
                   !observer.reject;
        },
        &observer);
    instrument.publishPendingState();
    if (observer.calls != 1 || observer.status != 0 ||
        instrument.pairWith(bridge.id().data()) != 0 || observer.calls != 2 || observer.status != 1)
        return false;
    observer.reject = true;
    std::thread removal([&] {
        bridge.reset();
        instrument.publishPendingState();
    });
    removal.join();
    if (observer.calls != 2 || instrument.allowsProjectOutput())
        return false;
    instrument.publishPendingState();
    if (observer.calls != 3 || observer.status != 2)
        return false;
    observer.reject = false;
    instrument.publishPendingState();
    instrument.publishPendingState();
    if (observer.calls != 4 || observer.status != 2 || observer.wrongThread ||
        instrument.unpair() != 0 || observer.calls != 5 || observer.status != 0 ||
        !instrument.allowsProjectOutput())
        return false;
    instrument.setStateChangedCallback(nullptr, nullptr);
    std::cout << "Cross-bundle pairing notifications: immediate delivery, foreign-thread deferral, "
                 "failed-delivery retry and missing-peer ownership passed (no GUI)\n";
    return true;
}

// Protocol-only callbacks: never create a native window or initialize Avalonia.
bool testPairViewLeases(TuneLab::Cubase::ManagedRuntime &runtime,
                        TuneLab::Cubase::ManagedRuntime &peer)
{
    using namespace TuneLab::Cubase;
    PairEndpoint instrument, bridge;
    if (!instrument.initialize(runtime, 1) || !bridge.initialize(peer, 2) ||
        instrument.pairWith(bridge.id().data()) != 0)
        return false;
    auto document = std::make_shared<EditorDocument>();
    document->runtime = peer;
    document->handle = 1234;
    document->runtime.createView = [](DocumentHandle id, void *, std::int32_t, std::int32_t,
                                      ViewHandle *view) -> std::int32_t {
        viewProbe.document = id;
        *view = ++viewProbe.created;
        return 0;
    };
    document->runtime.destroyView = [](ViewHandle) -> std::int32_t {
        if (viewProbe.rejectDestroy)
            return 4;
        ++viewProbe.destroyed;
        return 0;
    };
    document->runtime.resizeView = [](ViewHandle, std::int32_t, std::int32_t) -> std::int32_t {
        ++viewProbe.resized;
        return 0;
    };
    document->runtime.focusView = [](ViewHandle) -> std::int32_t {
        ++viewProbe.focused;
        return 0;
    };
    document->runtime.scaleView = [](ViewHandle, double factor) -> std::int32_t {
        viewProbe.scale = factor;
        return 0;
    };
    document->runtime.destroyDocument = [](DocumentHandle) -> std::int32_t {
        ++viewProbe.documentsReleased;
        return 0;
    };
    std::weak_ptr<EditorDocument> lifetime = document;
    if (PairViewOwner::registerWith(bridge, document) != 0)
        return false;
    DocumentHandle proxy = 0;
    ViewHandle first = 0;
    std::array<std::uint8_t, 16> second{}, rejected{};
    if (runtime.createPairDocumentProxy(instrument.id().data(), &proxy) != 0 || proxy == 0)
        return false;
    if (runtime.createView(proxy, reinterpret_cast<void *>(1), 900, 600, &first) != 0 ||
        peer.createPairView(bridge.id().data(), reinterpret_cast<void *>(1), 900, 600,
                            second.data()) != 0 ||
        first == 0 || runtime.destroyDocument(proxy) != 1 || viewProbe.created != 2 ||
        viewProbe.document != 1234)
        return false;
    auto host = Steinberg::owned(new Steinberg::Vst::HostApplication());
    auto controller = Steinberg::owned(new EditorController(true));
    if (controller->initialize(host) != Steinberg::kResultOk)
        return false;
    auto binding = Steinberg::owned(new Steinberg::Vst::HostMessage());
    binding->setMessageID("TuneLab.Pairing.State");
    binding->getAttributes()->setBinary("endpoint", instrument.id().data(), 16);
    binding->getAttributes()->setBinary("owner", bridge.id().data(), 16);
    binding->getAttributes()->setInt("status", 1);
    if (controller->notify(binding) != Steinberg::kResultOk)
        return false;
    auto view = Steinberg::owned(controller->createView(Steinberg::Vst::ViewType::kEditor));
    if (!view ||
        view->attached(reinterpret_cast<void *>(1), Steinberg::kPlatformTypeHWND) !=
            Steinberg::kResultOk ||
        viewProbe.created != 3 || viewProbe.document != 1234 ||
        controller->notify(binding) != Steinberg::kResultOk || viewProbe.created != 3)
        return false;
    Steinberg::ViewRect size(0, 0, 1000, 700);
    Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport> scaling(view);
    if (!scaling || scaling->setContentScaleFactor(1.5f) != Steinberg::kResultOk ||
        viewProbe.scale != 1.5 || runtime.scaleView(first, 1.25) != 0 || viewProbe.scale != 1.25 ||
        peer.scalePairView(second.data(), 2) != 0 || viewProbe.scale != 2)
        return false;
    if (view->onSize(&size) != Steinberg::kResultOk || view->onFocus(true) != Steinberg::kResultOk)
        return false;
    bridge.reset();
    document.reset();
    if (lifetime.expired() || viewProbe.documentsReleased != 0 ||
        runtime.createPairView(instrument.id().data(), reinterpret_cast<void *>(1), 900, 600,
                               rejected.data()) != 2 ||
        runtime.resizeView(first, 1000, 700) != 0 || runtime.focusView(first) != 0 ||
        runtime.scaleView(first, 1.75) != 0 || viewProbe.scale != 1.75 || viewProbe.resized != 2 ||
        viewProbe.focused != 2 || runtime.destroyView(first) != 4)
        return false;
    binding->getAttributes()->setInt("status", 2);
    // Missing-peer recovery now shows real pairing controls. Detach the fake
    // parent before selecting that normal managed session (GUI is skipped).
    if (view->removed() != Steinberg::kResultFalse)
        return false;
    viewProbe.rejectDestroy = false;
    if (view->removed() != Steinberg::kResultOk ||
        controller->notify(binding) != Steinberg::kResultOk)
        return false;
    binding->getAttributes()->setInt("status", 0);
    if (controller->notify(binding) != Steinberg::kResultOk ||
        controller->terminate() != Steinberg::kResultOk)
        return false;
    if (runtime.destroyView(first) != 0 || lifetime.expired() ||
        peer.destroyPairView(second.data()) != 0 || !lifetime.expired() ||
        viewProbe.destroyed != 3 || viewProbe.documentsReleased != 1 ||
        runtime.destroyDocument(proxy) != 0 || runtime.destroyDocument(proxy) != 2)
        return false;
    std::cout << "Cross-bundle controller and standard view proxy: same owner, resize/focus, "
                 "removal retention "
                 "and close retry passed (no GUI)\n";
    return true;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    const bool inspectPeer = argc == 4 && std::wcscmp(argv[2], L"--peer") == 0;
    if (!inspectPeer && argc != 2 &&
        ((argc != 3 && argc != 5) || std::wcscmp(argv[2], L"--services") != 0))
    {
        std::cerr << "Usage: tunelab_runtime_probe <managed-directory> [--services | --peer "
                     "<directory>]\n";
        return 1;
    }
    TuneLab::Cubase::ManagedRuntime runtime;
    if (!runtime.initialize(argv[1]))
    {
        std::cerr << "Managed runtime discovery or entry resolution failed\n";
        return 2;
    }
    if (argc >= 3 && !inspectPeer)
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
    {
        TuneLab::Cubase::PairEndpoint endpoint;
        std::array<std::uint8_t, 16> missing{};
        if (!endpoint.initialize(runtime, 1) ||
            runtime.setDocumentPairEndpoint(document, nullptr) != 1 ||
            runtime.setDocumentPairEndpoint(0, endpoint.id().data()) != 2 ||
            runtime.setDocumentPairEndpoint(document, missing.data()) != 2 ||
            runtime.setDocumentPairEndpoint(document, endpoint.id().data()) != 0)
            return 22;
        auto expired = endpoint.id();
        endpoint.reset();
        if (runtime.setDocumentPairEndpoint(document, expired.data()) != 2)
            return 23;
        std::cout
            << "Document pairing UI configuration validates live handles and endpoints (no GUI)\n";
    }
    double quarter = 0;
    if (runtime.mapDocumentSourcePosition(document, 0.75, &quarter) != 0 || quarter != 1.5 ||
        runtime.mapDocumentSourcePosition(0, 0.75, &quarter) != 2 ||
        runtime.mapDocumentSourcePosition(document, std::numeric_limits<double>::quiet_NaN(),
                                          &quarter) != 1)
        return 24;
    if (inspectPeer)
    {
        TuneLab::Cubase::ManagedRuntime peer;
        if (!peer.initialize(argv[3]))
            return 13;
        std::int32_t peerSize = 0;
        const auto result = peer.copyDocumentState(document, nullptr, 0, &peerSize);
        if (result != 0 &&
            result != static_cast<std::int32_t>(TuneLab::Cubase::BridgeResult::InvalidHandle))
            return 14;
        std::cout << "Peer bundle shares managed entry point: "
                  << (peer.createDocument == runtime.createDocument ? "yes" : "no")
                  << "; owner document visible in peer registry: " << (result == 0 ? "yes" : "no")
                  << '\n';
        std::array<std::uint8_t, 16> instrument{}, bridge{}, owner{};
        std::int32_t status = -1, output = -1;
        if (runtime.registerPairEndpoint(1, instrument.data()) != 0 ||
            peer.registerPairEndpoint(2, bridge.data()) != 0)
            return 15;
        std::atomic<bool> allowed{true};
        auto onOutput = [](void *owner, std::int32_t output) {
            static_cast<std::atomic<bool> *>(owner)->store(output != 0);
        };
        if (runtime.setPairOutputCallback(instrument.data(), onOutput, &allowed) != 0)
            return 18;
        std::vector<std::uint8_t> sharedState{4, 7, 9};
        struct Endpoints
        {
            TuneLab::Cubase::ManagedRuntime &instrumentRuntime, &bridgeRuntime;
            std::array<std::uint8_t, 16> &instrument, &bridge;
            ~Endpoints()
            {
                instrumentRuntime.releasePairEndpoint(instrument.data());
                bridgeRuntime.releasePairEndpoint(bridge.data());
            }
        } endpoints{runtime, peer, instrument, bridge};
        if (runtime.getPairEndpointState(instrument.data(), owner.data(), &status, &output) != 0 ||
            status != 0 || output != 1 ||
            runtime.pairEndpoints(instrument.data(), bridge.data()) != 0 ||
            peer.getPairEndpointState(instrument.data(), owner.data(), &status, &output) != 0 ||
            status != 1 || output != 0 || allowed.load() || owner != bridge ||
            runtime.getPairEndpointState(bridge.data(), owner.data(), &status, &output) != 0 ||
            status != 1 || output != 1 || owner != bridge)
            return 16;
        auto copyShared = [](void *owner, std::uint8_t *destination, std::int32_t capacity,
                             std::int32_t *required) -> std::int32_t {
            const auto &bytes = *static_cast<std::vector<std::uint8_t> *>(owner);
            *required = static_cast<std::int32_t>(bytes.size());
            if (!destination)
                return 0;
            if (capacity < *required)
                return 1;
            std::copy(bytes.begin(), bytes.end(), destination);
            return 0;
        };
        auto applyShared = [](void *owner, const std::uint8_t *bytes,
                              std::int32_t length) -> std::int32_t {
            static_cast<std::vector<std::uint8_t> *>(owner)->assign(bytes, bytes + length);
            return 0;
        };
        std::int32_t requiredState = 0;
        std::array<std::uint8_t, 3> copied{};
        const std::array<std::uint8_t, 2> replacement{2, 8};
        if (peer.setPairDocumentAccess(bridge.data(), copyShared, applyShared, &sharedState) != 0 ||
            runtime.copyPairDocumentState(instrument.data(), copied.data(), 3, &requiredState) !=
                0 ||
            requiredState != 3 || copied != std::array<std::uint8_t, 3>{4, 7, 9} ||
            runtime.applyPairDocumentState(instrument.data(), replacement.data(), 2) != 0 ||
            sharedState != std::vector<std::uint8_t>{2, 8})
            return 19;
        std::cout << "Cross-bundle document read/write delegates passed\n";
        if (peer.releasePairEndpoint(bridge.data()) != 0 ||
            runtime.getPairEndpointState(instrument.data(), owner.data(), &status, &output) != 0 ||
            status != 2 || output != 0 || allowed.load() ||
            owner != std::array<std::uint8_t, 16>{} ||
            peer.unpairEndpoint(instrument.data()) != 0 ||
            runtime.getPairEndpointState(instrument.data(), owner.data(), &status, &output) != 0 ||
            status != 0 || output != 1 || !allowed.load())
            return 17;
        std::cout << "Cross-bundle explicit pairing, single output owner, missing peer and unlink "
                     "passed\n";
        if (!testPairNotifications(runtime, peer))
            return 21;
        if (!testPairViewLeases(runtime, peer))
            return 20;
    }
    auto previewCallback = [](void *, std::int32_t, std::int32_t) {};
    if (runtime.setDocumentKeyPreviewCallback(document, previewCallback, nullptr) != 0 ||
        runtime.setDocumentKeyPreviewCallback(document, nullptr, nullptr) != 0 ||
        runtime.setDocumentKeyPreviewCallback(0, previewCallback, nullptr) != 2)
        return 11;
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
    if (runtime.mapDocumentSourcePosition(document, 0.75, &quarter) != 0 || quarter != 1.5)
        return 25;
    std::cout
        << "Source seconds map to local quarters in live and deferred document clocks (no GUI)\n";
    {
        using namespace TuneLab::Cubase;
        const AraTempoInput tempo[]{{10, 0}, {12, 4}, {16, 8}};
        const AraNoteInput note[]{{1, 0.25, 0.5, 60}};
        const AraRegionInput region{0.5, 2, 11.5, 2};
        std::int32_t count = 0;
        if (runtime.buildAraProjectState(nullptr, 0, tempo, 3, nullptr, 0, note, 1, &region,
                                         nullptr, 0, &count) != 0)
            return 26;
        std::vector<std::uint8_t> sourceState(count);
        if (runtime.buildAraProjectState(nullptr, 0, tempo, 3, nullptr, 0, note, 1, &region,
                                         sourceState.data(), count, &count) != 0 ||
            runtime.restoreDocumentState(document, sourceState.data(), count) != 0 ||
            runtime.mapDocumentSourcePosition(document, 1.5, &quarter) != 0 || quarter != 2.5 ||
            runtime.restoreDocumentState(document, state.data(), required) != 0)
            return 27;
        std::cout << "Deferred segmented source clock uses restored tempo rather than the old live "
                     "project (no GUI)\n";
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
    if (runtime.setDocumentKeyPreviewCallback(document, previewCallback, nullptr) != 2)
        return 12;
    std::cout << "Runtime discovered; document/view entry points resolved; document released\n";
    return 0;
}
