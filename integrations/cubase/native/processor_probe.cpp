#include "instrument_parameters.h"
#include "pair_endpoint.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "project_state_stream.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/vstcomponentbase.h"
#include <Windows.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

using namespace Steinberg;
using namespace Steinberg::Vst;

class TransportReceiver final : public ComponentBase
{
  public:
    std::array<std::uint8_t, 16> endpoint{};
    bool pairingReceived = false;
    int64 pairStatus = -1, pairOutput = -1;
    double position = 0;
    bool received = false;
    int64 song = -1, preview = -1, elapsed = 0;
    double tempo = 0, bar = 0, sampleRate = 0;
    int64 numerator = 0, denominator = 0, timeFlags = 0, samplePosition = 0;

    tresult PLUGIN_API notify(IMessage *message) override
    {
        if (message && message->getMessageID() &&
            std::strcmp(message->getMessageID(), "TuneLab.Pairing.State") == 0)
        {
            const void *bytes = nullptr;
            uint32 length = 0;
            if (message->getAttributes()->getBinary("endpoint", bytes, length) != kResultOk ||
                length != 16 || !bytes)
                return kResultFalse;
            std::copy_n(static_cast<const std::uint8_t *>(bytes), 16, endpoint.data());
            pairingReceived =
                message->getAttributes()->getInt("status", pairStatus) == kResultOk &&
                message->getAttributes()->getInt("projectOutput", pairOutput) == kResultOk;
            return pairingReceived ? kResultOk : kResultFalse;
        }
        if (!message || !message->getMessageID() ||
            std::strcmp(message->getMessageID(), "TuneLab.Transport.Position") != 0)
            return kResultFalse;
        auto *attributes = message->getAttributes();
        received = attributes->getFloat("quarterNotes", position) == kResultOk &&
                   attributes->getInt("songResult", song) == kResultOk &&
                   attributes->getInt("previewResult", preview) == kResultOk &&
                   attributes->getInt("previewMicroseconds", elapsed) == kResultOk;
        received = received && attributes->getFloat("tempo", tempo) == kResultOk &&
                   attributes->getFloat("barQuarterNotes", bar) == kResultOk &&
                   attributes->getFloat("sampleRate", sampleRate) == kResultOk &&
                   attributes->getInt("samplePosition", samplePosition) == kResultOk &&
                   attributes->getInt("timeFlags", timeFlags) == kResultOk &&
                   attributes->getInt("numerator", numerator) == kResultOk &&
                   attributes->getInt("denominator", denominator) == kResultOk;
        return received ? kResultOk : kResultFalse;
    }
};

struct PluginInstance
{
    HMODULE module = nullptr;
    bool(__cdecl *exitModule)() = nullptr;
    IPluginFactory *factory = nullptr;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    IEditController *controller = nullptr;
    bool controllerInitialized = false;
    bool initialized = false, active = false, processing = false;

    ~PluginInstance()
    {
        if (processing)
            processor->setProcessing(false);
        if (active)
            component->setActive(false);
        if (initialized)
            component->terminate();
        if (controllerInitialized)
            controller->terminate();
        if (controller)
            controller->release();
        if (processor)
            processor->release();
        if (component)
            component->release();
        if (factory)
            factory->release();
        if (exitModule)
            exitModule();
        if (module)
            FreeLibrary(module);
    }
};

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: tunelab_processor_probe <plugin-binary> <project-state>\n";
        return 1;
    }
    std::ifstream input(std::filesystem::path(argv[2]), std::ios::binary);
    std::vector<std::uint8_t> state((std::istreambuf_iterator<char>(input)), {});
    MemoryStream expectedState;
    TuneLab::Cubase::writeProjectState(&expectedState, state);
    TuneLab::Cubase::writeInstrumentParameters(&expectedState, false);
    auto host = owned(new HostApplication());
    PluginInstance plugin;
    plugin.module = LoadLibraryW(argv[1]);
    if (!plugin.module || state.empty())
        return 2;
    auto init = reinterpret_cast<bool(__cdecl *)()>(GetProcAddress(plugin.module, "InitDll"));
    auto factory = reinterpret_cast<IPluginFactory *(PLUGIN_API *)()>(
        GetProcAddress(plugin.module, "GetPluginFactory"));
    if (!init || !factory || !init())
        return 3;
    plugin.exitModule =
        reinterpret_cast<bool(__cdecl *)()>(GetProcAddress(plugin.module, "ExitDll"));
    plugin.factory = factory();
    const FUID id(0xAC17B8A2, 0x74DA4B52, 0xB7E1C5A9, 0x5DB09621);
    if (!plugin.factory ||
        plugin.factory->createInstance(id, IComponent::iid,
                                       reinterpret_cast<void **>(&plugin.component)) != kResultOk ||
        plugin.component->queryInterface(IAudioProcessor::iid,
                                         reinterpret_cast<void **>(&plugin.processor)) != kResultOk)
    {
        return 4;
    }
    if (plugin.component->initialize(host) != kResultOk)
        return 5;
    plugin.initialized = true;
    FUnknownPtr<IProcessContextRequirements> requirements(plugin.component);
    const uint32 requiredContext = IProcessContextRequirements::kNeedProjectTimeMusic |
                                   IProcessContextRequirements::kNeedTransportState;
    if (!requirements ||
        (requirements->getProcessContextRequirements() & requiredContext) != requiredContext)
    {
        std::cerr << "Processor does not request musical position and transport state\n";
        return 28;
    }
    const FUID controllerId(0x769C2F63, 0xE9474138, 0xA2B6D450, 0x817AC39D);
    if (plugin.factory->createInstance(controllerId, IEditController::iid,
                                       reinterpret_cast<void **>(&plugin.controller)) !=
            kResultOk ||
        plugin.controller->initialize(host) != kResultOk)
    {
        return 21;
    }
    plugin.controllerInitialized = true;
    ParameterInfo bypassInfo{};
    if (plugin.controller->getParameterCount() != 1 ||
        plugin.controller->getParameterInfo(0, bypassInfo) != kResultOk ||
        bypassInfo.id != TuneLab::Cubase::bypassParameterId || bypassInfo.stepCount != 1 ||
        !(bypassInfo.flags & ParameterInfo::kIsBypass))
        return 54;
    ProcessSetup setup{kOffline, kSample32, 512, 48000};
    if (plugin.processor->setupProcessing(setup) != kResultOk)
        return 6;
    MemoryStream source(state.data(), static_cast<TSize>(state.size()));
    // Load before connecting: connect must deliver the controller's latest snapshot.
    if (plugin.controller->setComponentState(&source) != kResultOk)
        return 7;
    IConnectionPoint *connectionRaw = nullptr;
    if (plugin.component->queryInterface(IConnectionPoint::iid,
                                         reinterpret_cast<void **>(&connectionRaw)) != kResultOk)
    {
        return 18;
    }
    auto connection = owned(connectionRaw);
    IConnectionPoint *controllerConnectionRaw = nullptr;
    if (plugin.controller->queryInterface(IConnectionPoint::iid,
                                          reinterpret_cast<void **>(&controllerConnectionRaw)) !=
        kResultOk)
    {
        return 22;
    }
    auto controllerConnection = owned(controllerConnectionRaw);
    if (connection->connect(controllerConnection) != kResultOk ||
        controllerConnection->connect(connection) != kResultOk)
    {
        return 23;
    }
    MemoryStream saved;
    if (plugin.component->getState(&saved) != kResultOk ||
        saved.getSize() != expectedState.getSize() ||
        std::memcmp(expectedState.getData(), saved.getData(), expectedState.getSize()) != 0)
        return 8;
    auto update = owned(Steinberg::Vst::allocateMessage(host));
    if (!update)
    {
        return 19;
    }
    update->setMessageID("TuneLab.ProjectState");
    if (update->getAttributes()->setBinary("state", state.data(),
                                           static_cast<uint32>(state.size())) != kResultOk ||
        connection->notify(update) != kResultOk)
    {
        return 20;
    }
    auto corrupt = state;
    std::fill(corrupt.begin() + 16, corrupt.end(), 0xFF);
    MemoryStream invalid(corrupt.data(), static_cast<TSize>(corrupt.size()));
    if (plugin.component->setState(&invalid) == kResultOk)
    {
        return 16;
    }
    MemoryStream afterInvalid;
    if (plugin.component->getState(&afterInvalid) != kResultOk ||
        afterInvalid.getSize() != expectedState.getSize() ||
        std::memcmp(expectedState.getData(), afterInvalid.getData(), expectedState.getSize()) != 0)
    {
        return 17;
    }
    if (plugin.component->setActive(true) != kResultOk)
        return 9;
    plugin.active = true;
    if (plugin.processor->setProcessing(true) != kResultOk)
        return 10;
    plugin.processing = true;
    std::vector<float> left(512), right(512);
    float *channels[]{left.data(), right.data()};
    AudioBusBuffers output{};
    output.numChannels = 2;
    output.channelBuffers32 = channels;
    ProcessContext context{};
    context.sampleRate = 48000;
    context.state = ProcessContext::kPlaying;
    ProcessData data{};
    data.processMode = kOffline;
    data.symbolicSampleSize = kSample32;
    data.numSamples = 512;
    data.numOutputs = 1;
    data.outputs = &output;
    data.processContext = &context;
    std::vector<float> referenceLeft;
    std::vector<float> referenceRight;
    double peak = 0;
    for (int position = 0; position < 144000; position += 512)
    {
        context.projectTimeSamples = position;
        if (plugin.processor->process(data) != kResultOk)
            return 11;
        referenceLeft.insert(referenceLeft.end(), left.begin(), left.end());
        referenceRight.insert(referenceRight.end(), right.begin(), right.end());
        for (int index = 0; index < 512; ++index)
        {
            if (!std::isfinite(left[index]) || !std::isfinite(right[index]))
                return 12;
            peak = (std::max)(peak, (std::max)(std::abs(double(left[index])),
                                               std::abs(double(right[index]))));
        }
    }
    // Revisit nonmonotonic sample positions with different host block sizes.
    // Exact equality against this instance's prepared cache detects stale
    // sequential-read cursors and off-by-one seek/loop errors.
    for (int32 blockSize : {64, 128, 256, 512})
    {
        data.numSamples = blockSize;
        for (TSamples position : {24000, 0, 48000, 24000, 143900})
        {
            context.projectTimeSamples = position;
            if (plugin.processor->process(data) != kResultOk)
                return 29;
            for (int32 index = 0; index < blockSize; ++index)
            {
                const auto sample = static_cast<size_t>(position + index);
                const float expectedLeft = sample < 144000 ? referenceLeft[sample] : 0.0f;
                const float expectedRight = sample < 144000 ? referenceRight[sample] : 0.0f;
                if (left[index] != expectedLeft || right[index] != expectedRight)
                {
                    std::cerr << "Seek/loop cache mismatch at sample " << sample << '\n';
                    return 30;
                }
            }
        }
    }
    data.numSamples = 512;
    ParameterChanges parameterChanges;
    int32 parameterIndex = 0;
    auto *bypassQueue =
        parameterChanges.addParameterData(TuneLab::Cubase::bypassParameterId, parameterIndex);
    bypassQueue->addPoint(127, 0, parameterIndex);
    bypassQueue->addPoint(128, 1, parameterIndex);
    bypassQueue->addPoint(255, 1, parameterIndex);
    bypassQueue->addPoint(256, 0, parameterIndex);
    data.inputParameterChanges = &parameterChanges;
    context.projectTimeSamples = 24000;
    if (plugin.processor->process(data) != kResultOk)
        return 55;
    for (int index = 0; index < 512; ++index)
    {
        const bool muted = index >= 128 && index < 256;
        if (left[index] != (muted ? 0 : referenceLeft[24000 + index]) ||
            right[index] != (muted ? 0 : referenceRight[24000 + index]))
            return 56;
    }
    parameterChanges.clearQueue();
    bypassQueue =
        parameterChanges.addParameterData(TuneLab::Cubase::bypassParameterId, parameterIndex);
    bypassQueue->addPoint(0, 1, parameterIndex);
    data.numSamples = 0;
    data.numOutputs = 0;
    if (plugin.processor->process(data) != kResultOk)
        return 57;
    MemoryStream bypassState;
    if (plugin.component->getState(&bypassState) != kResultOk)
        return 58;
    bypassState.seek(0, IBStream::kIBSeekSet, nullptr);
    if (plugin.controller->setComponentState(&bypassState) != kResultOk ||
        plugin.controller->getParamNormalized(TuneLab::Cubase::bypassParameterId) != 1)
        return 59;
    parameterChanges.clearQueue();
    bypassQueue =
        parameterChanges.addParameterData(TuneLab::Cubase::bypassParameterId, parameterIndex);
    bypassQueue->addPoint(0, 0, parameterIndex);
    if (plugin.processor->process(data) != kResultOk)
        return 63;
    bypassState.seek(0, IBStream::kIBSeekSet, nullptr);
    if (plugin.component->setState(&bypassState) != kResultOk)
        return 64;
    std::vector<char> malformedParameters(bypassState.getData(),
                                          bypassState.getData() + bypassState.getSize());
    malformedParameters.back() = 1;
    MemoryStream malformedParameterState(malformedParameters.data(),
                                         static_cast<TSize>(malformedParameters.size()));
    if (plugin.component->setState(&malformedParameterState) == kResultOk)
        return 65;
    if (connection->notify(update) != kResultOk)
        return 60;
    MemoryStream afterEditorUpdate;
    if (plugin.component->getState(&afterEditorUpdate) != kResultOk ||
        afterEditorUpdate.getSize() != bypassState.getSize() ||
        std::memcmp(afterEditorUpdate.getData(), bypassState.getData(), bypassState.getSize()) != 0)
        return 61;
    parameterChanges.clearQueue();
    bypassQueue =
        parameterChanges.addParameterData(TuneLab::Cubase::bypassParameterId, parameterIndex);
    bypassQueue->addPoint(0, 0, parameterIndex);
    if (plugin.processor->process(data) != kResultOk)
        return 62;
    data.numSamples = 512;
    data.numOutputs = 1;
    data.inputParameterChanges = nullptr;
    std::cout << "Bypass automation sample boundaries, versioned parameter restore and "
                 "editor-update preservation passed\n";
    context.state = 0;
    context.projectTimeSamples = 24000;
    if (plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 13;
    EventList midi;
    Event noteOn{};
    noteOn.type = Event::kNoteOnEvent;
    noteOn.flags = Event::kIsLive;
    noteOn.noteOn = {3, 67, 0, 0.8f, 0, 900};
    midi.addEvent(noteOn);
    data.inputEvents = &midi;
    const auto previewStart = std::chrono::steady_clock::now();
    if (plugin.processor->process(data) != kResultOk)
        return 37;
    midi.clear();
    bool previewAudible = false;
    while (std::chrono::steady_clock::now() - previewStart < std::chrono::seconds(30))
    {
        if (plugin.processor->process(data) != kResultOk)
            return 38;
        if (std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        {
            previewAudible = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!previewAudible)
    {
        std::cerr << "Live MIDI preview did not produce singing audio\n";
        return 39;
    }
    const auto coldPreviewMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - previewStart)
                                   .count();
    Event noteOff{};
    noteOff.type = Event::kNoteOffEvent;
    noteOff.sampleOffset = 37;
    noteOff.noteOff = {3, 67, 0, 900, 0};
    midi.addEvent(noteOff);
    if (plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin() + 37, left.end(), [](float value) { return value != 0; }) ||
        std::any_of(right.begin() + 37, right.end(), [](float value) { return value != 0; }))
    {
        return 40;
    }
    midi.clear();
    for (int index = 0; index < 8; ++index)
    {
        if (plugin.processor->process(data) != kResultOk ||
            std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
            return 41;
    }
    midi.addEvent(noteOn);
    bool warmAudible = false;
    for (int index = 0; index < 32; ++index)
    {
        if (plugin.processor->process(data) != kResultOk)
            return 42;
        midi.clear();
        warmAudible = warmAudible ||
                      std::any_of(left.begin(), left.end(), [](float value) { return value != 0; });
    }
    if (!warmAudible)
        return 43;
    noteOff.sampleOffset = 0;
    midi.addEvent(noteOff);
    if (plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 44;
    midi.clear();
    midi.addEvent(noteOn);
    if (plugin.processor->process(data) != kResultOk)
        return 45;
    midi.clear();
    midi.addEvent(noteOff);
    data.numOutputs = 0;
    data.numSamples = 0;
    if (plugin.processor->process(data) != kResultOk)
        return 46;
    midi.clear();
    data.numOutputs = 1;
    data.numSamples = 512;
    if (plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 47;
    data.inputEvents = nullptr;
    auto transport = owned(new TransportReceiver());
    connection->disconnect(controllerConnection);
    controllerConnection->disconnect(connection);
    if (connection->connect(transport) != kResultOk)
        return 24;
    auto request = owned(Steinberg::Vst::allocateMessage(host));
    request->setMessageID("TuneLab.Transport.Request");
    // Editor-state updates above enqueue a new asynchronous whole-score render.
    // Wait for that real task before asserting its completed status.
    const auto songDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    do
    {
        if (connection->notify(request) != kResultOk)
            return 48;
        if (transport->song != static_cast<int64>(TuneLab::Cubase::BridgeResult::Busy))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < songDeadline);
    if (!transport->received || transport->song != 0 || transport->preview != 0 ||
        transport->elapsed <= 0)
    {
        std::cerr << "Transport preview status: received=" << transport->received
                  << " song=" << transport->song << " preview=" << transport->preview
                  << " elapsed=" << transport->elapsed << '\n';
        return 48;
    }
    std::cout << "Live MIDI singing preview cold onset=" << coldPreviewMs
              << "ms; warm cache replay and sample-offset Note Off passed\n";
    context.state = ProcessContext::kPlaying;
    context.projectTimeSamples = 24000;
    if (plugin.processor->process(data) != kResultOk ||
        std::none_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 66;
    const auto independentLeft = left;
    auto pairingRequest = owned(Steinberg::Vst::allocateMessage(host));
    pairingRequest->setMessageID("TuneLab.Pairing.Request");
    if (connection->notify(pairingRequest) != kResultOk || !transport->pairingReceived ||
        transport->pairStatus != 0 || transport->pairOutput != 1)
        return 67;
    TuneLab::Cubase::ManagedRuntime pairingRuntime;
    const auto managed =
        std::filesystem::path(argv[1]).parent_path().parent_path() / L"Resources" / L"managed";
    TuneLab::Cubase::PairEndpoint companion;
    if (!pairingRuntime.initialize(managed) || !companion.initialize(pairingRuntime, 2))
        return 68;
    auto bind = owned(Steinberg::Vst::allocateMessage(host));
    bind->setMessageID("TuneLab.Pairing.Bind");
    bind->getAttributes()->setBinary("bridge", companion.id().data(), 16);
    transport->pairingReceived = false;
    if (connection->notify(bind) != kResultOk || !transport->pairingReceived ||
        transport->pairStatus != 1 || transport->pairOutput != 0 ||
        plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 69;
    MemoryStream pairedSaved;
    if (plugin.component->getState(&pairedSaved) != kResultOk)
        return 74;
    pairedSaved.seek(0, IBStream::kIBSeekSet, nullptr);
    std::vector<std::uint8_t> pairedScore;
    bool savedBypass = false, savedIntent = false;
    if (TuneLab::Cubase::readProjectState(&pairedSaved, pairedScore) != kResultOk ||
        TuneLab::Cubase::readInstrumentParameters(&pairedSaved, savedBypass, savedIntent) !=
            kResultOk ||
        !savedIntent || pairedScore != state)
        return 75;
    pairedSaved.seek(0, IBStream::kIBSeekSet, nullptr);
    if (plugin.component->setState(&pairedSaved) != kResultOk || transport->pairStatus != 2 ||
        transport->pairOutput != 0 || plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 76;
    std::array<std::uint8_t, 16> formerOwner{};
    std::int32_t formerStatus = -1, formerOutput = -1;
    if (companion.state(formerOwner, formerStatus, formerOutput) != 0 || formerStatus != 0 ||
        formerOutput != 1)
        return 77;
    const auto restoreDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    do
    {
        if (connection->notify(request) != kResultOk)
            return 78;
        if (transport->song != static_cast<int64>(TuneLab::Cubase::BridgeResult::Busy))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < restoreDeadline);
    if (transport->song != 0 || connection->notify(bind) != kResultOk ||
        transport->pairStatus != 1 || transport->pairOutput != 0)
        return 79;
    std::cout << "Paired component state preserves muted intent, detaches live peer and requires "
                 "explicit rebind\n";
    EventList pairedMidi;
    pairedMidi.addEvent(noteOn);
    data.inputEvents = &pairedMidi;
    if (plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 72;
    data.inputEvents = nullptr;
    for (int block = 0; block < 64; ++block)
        if (plugin.processor->process(data) != kResultOk ||
            std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
            return 73;
    transport->pairingReceived = false;
    companion.reset();
    if (!transport->pairingReceived || transport->pairStatus != 2 || transport->pairOutput != 0 ||
        plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 70;
    auto unlink = owned(Steinberg::Vst::allocateMessage(host));
    unlink->setMessageID("TuneLab.Pairing.Unlink");
    transport->pairingReceived = false;
    if (connection->notify(unlink) != kResultOk || !transport->pairingReceived ||
        transport->pairStatus != 0 || transport->pairOutput != 1 ||
        plugin.processor->process(data) != kResultOk || left != independentLeft)
        return 71;
    std::cout << "Actual instrument pairing callback mutes project output, missing peer stays "
                 "muted, explicit unlink restores cache\n";
    MemoryStream empty;
    TuneLab::Cubase::writeProbeState(&empty);
    empty.seek(0, IBStream::kIBSeekSet, nullptr);
    if (plugin.component->setState(&empty) != kResultOk ||
        plugin.processor->process(data) != kResultOk ||
        std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }))
        return 14;
    // Poll on this control thread, after process has published its position.
    context.state = ProcessContext::kProjectTimeMusicValid | ProcessContext::kTempoValid |
                    ProcessContext::kTimeSigValid | ProcessContext::kBarPositionValid;
    context.tempo = 137.5;
    context.timeSigNumerator = 7;
    context.timeSigDenominator = 8;
    context.barPositionMusic = -3.5;
    context.projectTimeMusic = -2.5;
    if (plugin.processor->process(data) != kResultOk || connection->notify(request) != kResultOk ||
        !transport->received || transport->position != -2.5 || transport->song != 0 ||
        transport->preview != -1 || transport->elapsed != 0 || transport->tempo != 137.5 ||
        transport->numerator != 7 || transport->denominator != 8 || transport->bar != -3.5 ||
        transport->sampleRate != 48000 || transport->samplePosition != context.projectTimeSamples)
        return 25;
    context.projectTimeMusic = 7.25;
    if (plugin.processor->process(data) != kResultOk || connection->notify(request) != kResultOk ||
        transport->position != 7.25)
        return 26;
    context.state = 0;
    if (plugin.processor->process(data) != kResultOk || connection->notify(request) != kResultOk ||
        !std::isnan(transport->position) || !std::isnan(transport->tempo) ||
        transport->numerator != 0 || transport->denominator != 0)
        return 27;
    connection->disconnect(transport);

    // A second component shares the loaded module and engine services, but
    // owns its document/render worker/cache. Keep the first instance empty.
    PluginInstance second;
    if (plugin.factory->createInstance(id, IComponent::iid,
                                       reinterpret_cast<void **>(&second.component)) != kResultOk ||
        second.component->queryInterface(
            IAudioProcessor::iid, reinterpret_cast<void **>(&second.processor)) != kResultOk ||
        second.component->initialize(host) != kResultOk)
    {
        return 31;
    }
    second.initialized = true;
    ProcessSetup secondSetup{kOffline, kSample64, 512, 44100};
    MemoryStream secondSource(state.data(), static_cast<TSize>(state.size()));
    if (second.processor->setupProcessing(secondSetup) != kResultOk ||
        second.component->setState(&secondSource) != kResultOk ||
        second.component->setActive(true) != kResultOk)
    {
        return 32;
    }
    second.active = true;
    if (second.processor->setProcessing(true) != kResultOk)
        return 33;
    second.processing = true;
    std::vector<double> secondLeft(512), secondRight(512);
    double *secondChannels[]{secondLeft.data(), secondRight.data()};
    AudioBusBuffers secondOutput{};
    secondOutput.numChannels = 2;
    secondOutput.channelBuffers64 = secondChannels;
    ProcessContext secondContext{};
    secondContext.sampleRate = 44100;
    secondContext.state = ProcessContext::kPlaying;
    ProcessData secondData{};
    secondData.processMode = kOffline;
    secondData.symbolicSampleSize = kSample64;
    secondData.numSamples = 512;
    secondData.numOutputs = 1;
    secondData.outputs = &secondOutput;
    secondData.processContext = &secondContext;
    double secondPeak = 0;
    context.state = ProcessContext::kPlaying;
    for (TSamples position = 0; position < 132300; position += 512)
    {
        secondContext.projectTimeSamples = position;
        context.projectTimeSamples = position;
        if (second.processor->process(secondData) != kResultOk ||
            plugin.processor->process(data) != kResultOk ||
            std::any_of(left.begin(), left.end(), [](float value) { return value != 0; }) ||
            std::any_of(right.begin(), right.end(), [](float value) { return value != 0; }))
        {
            return 34;
        }
        for (int index = 0; index < 512; ++index)
        {
            if (!std::isfinite(secondLeft[index]) || !std::isfinite(secondRight[index]))
                return 35;
            secondPeak = (std::max)(secondPeak, (std::max)(std::abs(secondLeft[index]),
                                                           std::abs(secondRight[index])));
        }
    }
    MemoryStream secondSaved;
    if (secondPeak == 0 || second.component->getState(&secondSaved) != kResultOk ||
        secondSaved.getSize() != expectedState.getSize() ||
        std::memcmp(expectedState.getData(), secondSaved.getData(), expectedState.getSize()) != 0)
    {
        return 36;
    }
    EventList doubleMidi;
    secondContext.state = 0;
    secondData.inputEvents = &doubleMidi;
    doubleMidi.addEvent(noteOn);
    if (second.processor->process(secondData) != kResultOk)
        return 49;
    doubleMidi.clear();
    bool doublePreviewAudible = false;
    const auto doublePreviewStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - doublePreviewStart < std::chrono::seconds(30))
    {
        if (second.processor->process(secondData) != kResultOk)
            return 50;
        for (int index = 0; index < 512; ++index)
        {
            if (!std::isfinite(secondLeft[index]) || !std::isfinite(secondRight[index]))
                return 51;
            doublePreviewAudible =
                doublePreviewAudible || secondLeft[index] != 0 || secondRight[index] != 0;
        }
        if (doublePreviewAudible)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!doublePreviewAudible)
        return 52;
    noteOff.sampleOffset = 37;
    doubleMidi.addEvent(noteOff);
    if (second.processor->process(secondData) != kResultOk ||
        std::any_of(secondLeft.begin() + 37, secondLeft.end(),
                    [](double value) { return value != 0; }) ||
        std::any_of(secondRight.begin() + 37, secondRight.end(),
                    [](double value) { return value != 0; }))
    {
        return 53;
    }
    std::cout << "44.1k/64-bit live MIDI singing output and sample-offset Note Off passed\n";
    pairedSaved.seek(0, IBStream::kIBSeekSet, nullptr);
    if (second.component->setState(&pairedSaved) != kResultOk ||
        second.processor->process(secondData) != kResultOk ||
        std::any_of(secondLeft.begin(), secondLeft.end(), [](double value) { return value != 0; }))
        return 80;
    std::cout << "Copied paired state stays muted in an independent instance without auto-claiming "
                 "a peer\n";
    std::cout << "VST3 processor offline output peak=" << peak
              << "; second instance 44.1k/64-bit peak=" << secondPeak
              << "; controller replay, state, seek/loop, instance isolation and transport polling "
                 "passed\n";
    return peak > 0 ? 0 : 15;
}
