#include "ARA_API/ARAVST3.h"
#include "ara_document.h"
#include "ara_playback.h"
#include "ara_source_identity.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

using namespace ARA;
using namespace TuneLab::Cubase;

extern "C" Steinberg::IPluginFactory *PLUGIN_API GetPluginFactory();

namespace
{
RestoreDocumentState originalRestore = nullptr;
RestoreDocumentState originalAraRestore = nullptr;
bool busyNextAraRestore = false;
std::int32_t __cdecl busyOnceAraRestore(DocumentHandle document, const std::uint8_t *state,
                                        std::int32_t length)
{
    if (busyNextAraRestore)
    {
        busyNextAraRestore = false;
        return static_cast<std::int32_t>(BridgeResult::Busy);
    }
    return originalAraRestore(document, state, length);
}
bool rejectNextRestore = false;
std::int32_t __cdecl failOnceRestore(DocumentHandle document, const std::uint8_t *state,
                                     std::int32_t length)
{
    if (rejectNextRestore)
    {
        rejectNextRestore = false;
        return static_cast<std::int32_t>(BridgeResult::Failed);
    }
    return originalRestore(document, state, length);
}

void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct Archive
{
    std::vector<std::uint8_t> bytes;
};

ARASize ARA_CALL archiveSize(ARAArchivingControllerHostRef, ARAArchiveReaderHostRef ref)
{
    return reinterpret_cast<Archive *>(ref)->bytes.size();
}
ARABool ARA_CALL readArchive(ARAArchivingControllerHostRef, ARAArchiveReaderHostRef ref,
                             ARASize position, ARASize length, ARAByte *buffer)
{
    const auto &bytes = reinterpret_cast<Archive *>(ref)->bytes;
    if (position > bytes.size() || length > bytes.size() - position)
        return kARAFalse;
    std::memcpy(buffer, bytes.data() + position, length);
    return kARATrue;
}
ARABool ARA_CALL writeArchive(ARAArchivingControllerHostRef, ARAArchiveWriterHostRef ref,
                              ARASize position, ARASize length, const ARAByte *buffer)
{
    auto &bytes = reinterpret_cast<Archive *>(ref)->bytes;
    bytes.resize(position + length);
    std::memcpy(bytes.data() + position, buffer, length);
    return kARATrue;
}
void ARA_CALL progress(ARAArchivingControllerHostRef, float)
{
}
ARAPersistentID ARA_CALL archiveID(ARAArchivingControllerHostRef, ARAArchiveReaderHostRef)
{
    return getProbeFactory()->documentArchiveID;
}
ARAAudioReaderHostRef ARA_CALL createReader(ARAAudioAccessControllerHostRef, ARAAudioSourceHostRef,
                                            ARABool)
{
    throw std::runtime_error("Archive fixture unexpectedly requested audio");
}
ARABool ARA_CALL readSamples(ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef,
                             ARASamplePosition, ARASampleCount, void *const *)
{
    return kARAFalse;
}
void ARA_CALL destroyReader(ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef)
{
}

struct Document
{
    std::vector<ARAContentTempoEntry> tempoPoints{{10, 0}, {12, 4}, {16, 8}};
    std::vector<ARAContentBarSignature> signatures{{4, 4, -4}, {3, 4, 4}};
    std::vector<ARAContentNote> sourceNotes{{261.6256f, 60, 0.7f, 0.25, 0, 0.25, 0.3},
                                            {261.6256f, 60, 0.9f, 0.25, 0, 0.5, 0.6}};
    bool sourceNotesAvailable = true;
    struct ContentReader
    {
        Document *owner;
        ARAContentType type;
    };
    int contentReaders = 0;
    struct ModificationUpdate
    {
        ARAAudioModificationHostRef modification;
        bool wholeRange;
        ARAContentUpdateFlags flags;
    };
    std::vector<ModificationUpdate> modificationUpdates;
    int sourceUpdates = 0, regionUpdates = 0, documentUpdates = 0;
    ARAModelUpdateControllerInterface modelUpdates{};
    ARAContentAccessControllerInterface content{};
    ARAAudioAccessControllerInterface audio{sizeof(audio), createReader, readSamples,
                                            destroyReader};
    ARAArchivingControllerInterface archiving{
        sizeof(archiving), archiveSize, readArchive, writeArchive, progress, progress, archiveID};
    ARADocumentControllerHostInstance host{};
    const ARADocumentControllerInstance *instance;
    ARAAudioSourceRef source;
    std::vector<ARAAudioModificationRef> modifications;
    int tokens[4]{};

    explicit Document(const char *sourceId = "source")
    {
        host.structSize = sizeof(host);
        host.audioAccessControllerHostRef = reinterpret_cast<ARAAudioAccessControllerHostRef>(this);
        host.audioAccessControllerInterface = &audio;
        host.archivingControllerHostRef = reinterpret_cast<ARAArchivingControllerHostRef>(this);
        host.archivingControllerInterface = &archiving;
        modelUpdates.structSize = sizeof(modelUpdates);
        modelUpdates.notifyAudioSourceAnalysisProgress = [](ARAModelUpdateControllerHostRef,
                                                            ARAAudioSourceHostRef,
                                                            ARAAnalysisProgressState, float) {};
        modelUpdates.notifyAudioSourceContentChanged =
            [](ARAModelUpdateControllerHostRef owner, ARAAudioSourceHostRef,
               const ARAContentTimeRange *,
               ARAContentUpdateFlags) { ++reinterpret_cast<Document *>(owner)->sourceUpdates; };
        modelUpdates.notifyAudioModificationContentChanged =
            [](ARAModelUpdateControllerHostRef owner, ARAAudioModificationHostRef modification,
               const ARAContentTimeRange *range, ARAContentUpdateFlags flags) {
                reinterpret_cast<Document *>(owner)->modificationUpdates.push_back(
                    {modification, range == nullptr, flags});
            };
        modelUpdates.notifyPlaybackRegionContentChanged =
            [](ARAModelUpdateControllerHostRef owner, ARAPlaybackRegionHostRef,
               const ARAContentTimeRange *,
               ARAContentUpdateFlags) { ++reinterpret_cast<Document *>(owner)->regionUpdates; };
        modelUpdates.notifyDocumentDataChanged = [](ARAModelUpdateControllerHostRef owner) {
            ++reinterpret_cast<Document *>(owner)->documentUpdates;
        };
        host.modelUpdateControllerHostRef = reinterpret_cast<ARAModelUpdateControllerHostRef>(this);
        host.modelUpdateControllerInterface = &modelUpdates;
        content.structSize = sizeof(content);
        content.isMusicalContextContentAvailable = [](ARAContentAccessControllerHostRef,
                                                      ARAMusicalContextHostRef,
                                                      ARAContentType type) -> ARABool {
            return type == kARAContentTypeTempoEntries || type == kARAContentTypeBarSignatures
                       ? kARATrue
                       : kARAFalse;
        };
        content.getMusicalContextContentGrade =
            [](ARAContentAccessControllerHostRef, ARAMusicalContextHostRef,
               ARAContentType) -> ARAContentGrade { return kARAContentGradeAdjusted; };
        content.createMusicalContextContentReader =
            [](ARAContentAccessControllerHostRef owner, ARAMusicalContextHostRef,
               ARAContentType type, const ARAContentTimeRange *) {
                auto *document = reinterpret_cast<Document *>(owner);
                ++document->contentReaders;
                return reinterpret_cast<ARAContentReaderHostRef>(new ContentReader{document, type});
            };
        content.isAudioSourceContentAvailable = [](ARAContentAccessControllerHostRef owner,
                                                   ARAAudioSourceHostRef,
                                                   ARAContentType type) -> ARABool {
            return type == kARAContentTypeNotes &&
                           reinterpret_cast<Document *>(owner)->sourceNotesAvailable
                       ? kARATrue
                       : kARAFalse;
        };
        content.getAudioSourceContentGrade =
            [](ARAContentAccessControllerHostRef, ARAAudioSourceHostRef,
               ARAContentType) -> ARAContentGrade { return kARAContentGradeInitial; };
        content.createAudioSourceContentReader =
            [](ARAContentAccessControllerHostRef owner, ARAAudioSourceHostRef, ARAContentType type,
               const ARAContentTimeRange *) -> ARAContentReaderHostRef {
            auto *document = reinterpret_cast<Document *>(owner);
            ++document->contentReaders;
            return reinterpret_cast<ARAContentReaderHostRef>(new ContentReader{document, type});
        };
        content.getContentReaderEventCount = [](ARAContentAccessControllerHostRef,
                                                ARAContentReaderHostRef reader) -> ARAInt32 {
            const auto *contentReader = reinterpret_cast<ContentReader *>(reader);
            if (contentReader->type == kARAContentTypeNotes)
                return static_cast<ARAInt32>(contentReader->owner->sourceNotes.size());
            return static_cast<ARAInt32>(contentReader->type == kARAContentTypeTempoEntries
                                             ? contentReader->owner->tempoPoints.size()
                                             : contentReader->owner->signatures.size());
        };
        content.getContentReaderDataForEvent = [](ARAContentAccessControllerHostRef,
                                                  ARAContentReaderHostRef reader,
                                                  ARAInt32 index) -> const void * {
            const auto *contentReader = reinterpret_cast<ContentReader *>(reader);
            if (contentReader->type == kARAContentTypeNotes)
                return &contentReader->owner->sourceNotes.at(index);
            if (contentReader->type == kARAContentTypeTempoEntries)
                return &contentReader->owner->tempoPoints.at(index);
            return &contentReader->owner->signatures.at(index);
        };
        content.destroyContentReader = [](ARAContentAccessControllerHostRef,
                                          ARAContentReaderHostRef reader) {
            auto *contentReader = reinterpret_cast<ContentReader *>(reader);
            --contentReader->owner->contentReaders;
            delete contentReader;
        };
        host.contentAccessControllerHostRef =
            reinterpret_cast<ARAContentAccessControllerHostRef>(this);
        host.contentAccessControllerInterface = &content;
        ARADocumentProperties properties{sizeof(properties), "Archive fixture"};
        instance = getProbeFactory()->createDocumentControllerWithDocument(&host, &properties);
        require(instance != nullptr, "Document creation failed");
        api()->beginEditing(ref());
        ARAAudioSourceProperties sourceProperties{};
        sourceProperties.structSize = sizeof(sourceProperties);
        sourceProperties.name = "Source";
        sourceProperties.persistentID = sourceId;
        sourceProperties.sampleCount = 44100;
        sourceProperties.sampleRate = 44100;
        sourceProperties.channelCount = 1;
        source = api()->createAudioSource(
            ref(), reinterpret_cast<ARAAudioSourceHostRef>(&tokens[0]), &sourceProperties);
        api()->endEditing(ref());
    }
    ~Document()
    {
        auto *model = PlugIn::fromRef<ProbeDocumentController>(ref());
        if (!model->isHostEditingDocument())
            api()->beginEditing(ref());
        for (auto modification : modifications)
        {
            const auto regions = PlugIn::fromRef(modification)->getPlaybackRegions();
            for (auto *region : regions)
                api()->destroyPlaybackRegion(ref(), PlugIn::toRef(region));
        }
        for (auto modification : modifications)
            api()->destroyAudioModification(ref(), modification);
        api()->destroyAudioSource(ref(), source);
        const auto sequences = model->getDocument()->getRegionSequences();
        for (auto *sequence : sequences)
            api()->destroyRegionSequence(ref(), PlugIn::toRef(sequence));
        const auto contexts = model->getDocument()->getMusicalContexts();
        for (auto *context : contexts)
            api()->destroyMusicalContext(ref(), PlugIn::toRef(context));
        api()->endEditing(ref());
        api()->destroyDocumentController(ref());
    }
    auto api() const
    {
        return instance->documentControllerInterface;
    }
    auto ref() const
    {
        return instance->documentControllerRef;
    }
    ARAAudioModificationRef add(const char *id, ARAAudioModificationRef clone = nullptr)
    {
        api()->beginEditing(ref());
        ARAAudioModificationProperties properties{sizeof(properties), id, id};
        auto hostRef =
            reinterpret_cast<ARAAudioModificationHostRef>(&tokens[modifications.size() + 1]);
        auto modification =
            clone ? api()->cloneAudioModification(ref(), clone, hostRef, &properties)
                  : api()->createAudioModification(ref(), source, hostRef, &properties);
        modifications.push_back(modification);
        api()->endEditing(ref());
        return modification;
    }
    bool restore(Archive &archive, const ARARestoreObjectsFilter &filter)
    {
        api()->beginEditing(ref());
        auto result = api()->restoreObjectsFromArchive(
            ref(), reinterpret_cast<ARAArchiveReaderHostRef>(&archive), &filter);
        api()->endEditing(ref());
        return result == kARATrue;
    }
};

auto editor(ARAAudioModificationRef ref)
{
    return PlugIn::fromRef<BridgeAudioModification>(ref)->editorDocument();
}

DocumentHandle lastViewDocument = 0;
int removedProxyViews = 0;
std::int32_t __cdecl fakeCreateView(DocumentHandle document, void *, std::int32_t, std::int32_t,
                                    ViewHandle *view)
{
    lastViewDocument = document;
    *view = document;
    return 0;
}

void testProxyBinding(Document &document, ARAPlaybackRegionRef region,
                      ARAAudioModificationRef modification, bool controllerFirst)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    auto factory = owned(GetPluginFactory());
    auto host = owned(new HostApplication());
    PClassInfo processorClass{}, controllerClass{};
    require(factory->getClassInfo(0, &processorClass) == kResultOk &&
                factory->getClassInfo(1, &controllerClass) == kResultOk,
            "Bridge factory classes unavailable");
    IComponent *componentRaw = nullptr;
    IEditController *controllerRaw = nullptr;
    require(factory->createInstance(processorClass.cid, IComponent::iid,
                                    reinterpret_cast<void **>(&componentRaw)) == kResultOk &&
                factory->createInstance(controllerClass.cid, IEditController::iid,
                                        reinterpret_cast<void **>(&controllerRaw)) == kResultOk,
            "Bridge component creation failed");
    auto component = owned(componentRaw);
    auto controller = owned(controllerRaw);
    require(component->initialize(host) == kResultOk && controller->initialize(host) == kResultOk,
            "Bridge initialization failed");
    FUnknownPtr<IConnectionPoint> processorConnection(component);
    FUnknownPtr<IConnectionPoint> controllerConnection(controller);
    FUnknownPtr<ARA::IPlugInEntryPoint2> entry(component);
    require(entry && processorConnection && controllerConnection, "Bridge interfaces unavailable");
    const auto *extension = entry->bindToDocumentControllerWithRoles(
        document.ref(), kARAEditorViewRole, kARAEditorViewRole);
    require(extension && extension->editorViewRef, "Bridge editor role binding failed");
    auto fromProcessor = owned(new ConnectionProxy(processorConnection));
    auto fromController = owned(new ConnectionProxy(controllerConnection));
    if (controllerFirst)
    {
        require(fromController->connect(processorConnection) == kResultOk,
                "Controller connect failed");
        require(fromProcessor->connect(controllerConnection) == kResultOk,
                "Processor connect failed");
    }
    else
    {
        require(fromProcessor->connect(controllerConnection) == kResultOk,
                "Processor connect failed");
        require(fromController->connect(processorConnection) == kResultOk,
                "Controller connect failed");
    }
    auto view = owned(controller->createView(ViewType::kEditor));
    require(view && view->attached(reinterpret_cast<void *>(1), kPlatformTypeHWND) == kResultOk,
            "Protocol-only parent attach failed");
    auto selected = editor(modification);
    const auto originalCreate = selected->runtime.createView;
    const auto originalDestroy = selected->runtime.destroyView;
    selected->runtime.createView = fakeCreateView;
    selected->runtime.destroyView = [](auto) -> std::int32_t {
        ++removedProxyViews;
        return 0;
    };
    lastViewDocument = 0;
    ARAViewSelection selection{};
    selection.structSize = sizeof(selection);
    selection.playbackRegionRefsCount = 1;
    selection.playbackRegionRefs = &region;
    extension->editorViewInterface->notifySelection(extension->editorViewRef, &selection);
    require(lastViewDocument == selected->handle,
            "Proxy selection did not reach actual controller view");
    FUnknownPtr<IAudioProcessor> audio(component);
    auto *regionModel = PlugIn::fromRef<BridgePlaybackRegion>(region);
    require(audio && selected->sourcePosition && selected->pollPosition &&
                std::isnan(selected->pollPosition()),
            "Bridge playhead did not start without a host position");
    ProcessContext position{};
    position.sampleRate = 48000;
    position.projectTimeSamples = static_cast<int64>(std::llround(
        (regionModel->getStartInPlaybackTime() + regionModel->getDurationInPlaybackTime() * 0.5) *
        position.sampleRate));
    ProcessData tick{};
    tick.processContext = &position;
    require(audio->process(tick) == kResultOk &&
                std::abs(selected->pollPosition() -
                         (regionModel->getStartInAudioModificationTime() +
                          regionModel->getDurationInAudioModificationTime() * 0.5)) < 1e-5 &&
                std::isfinite(EditorDocument::onPositionRequested(selected.get())),
            "Bridge transport did not map to source seconds and local quarters");
    tick.processContext = nullptr;
    require(audio->process(tick) == kResultOk && std::isnan(selected->pollPosition()),
            "Bridge playhead retained invalid host timing");
    std::cout
        << "PASS: actual Bridge source playhead transport and invalid-context clearing (no GUI)\n";
    const int removedBeforeDisconnect = removedProxyViews;
    require(fromController->disconnect(), "Controller disconnect failed");
    require(removedProxyViews == removedBeforeDisconnect + 1,
            "Controller disconnect retained selected document view");
    require(fromProcessor->disconnect(), "Processor disconnect failed");
    lastViewDocument = 0;
    extension->editorViewInterface->notifySelection(extension->editorViewRef, &selection);
    require(lastViewDocument == 0, "Disconnected selection recreated a view");
    require(fromProcessor->connect(controllerConnection) == kResultOk &&
                fromController->connect(processorConnection) == kResultOk,
            "Proxy reconnect failed");
    extension->editorViewInterface->notifySelection(extension->editorViewRef, &selection);
    require(lastViewDocument == selected->handle,
            "Reconnected selection did not restore document view");
    require(fromProcessor->disconnect() && fromController->disconnect(),
            "Reconnected proxy cleanup failed");
    require(view->removed() == kResultOk, "Protocol view removal failed");
    view.reset();
    selected->runtime.createView = originalCreate;
    selected->runtime.destroyView = originalDestroy;
    require(controller->terminate() == kResultOk && component->terminate() == kResultOk,
            "Bridge termination failed");
}

void testRegionSynthesis(ManagedRuntime runtime, const std::vector<std::uint8_t> &state)
{
    constexpr int rate = 48000;
    constexpr int count = 512;
    RenderWorker reference(runtime);
    auto generation = reference.submit(state, rate);
    require(reference.wait(generation, std::chrono::minutes(5)) && reference.result() == 0,
            "ARA reference synthesis failed");
    AraPlayback playback;
    playback.prepare(count, rate);
    playback.add(runtime, state, 0.25, 2, 0.5, true);
    playback.add(runtime, state, 0.25, 2, 0.5, true);
    playback.publish();
    std::array<float, count> expectedLeft{}, expectedRight{};
    std::array<double, count> left{}, right{};
    bool foundVoice = false;
    for (int offset = -256; offset < rate / 2 + count; offset += count)
    {
        left.fill(0);
        right.fill(0);
        const auto songPosition = 2 * rate + offset;
        playback.mix(left.data(), right.data(), count, songPosition);
        reference.copyStereo(expectedLeft.data(), expectedRight.data(), count, rate / 4 + offset,
                             rate);
        for (int index = 0; index < count; ++index)
        {
            const bool inside = offset + index >= 0 && offset + index < rate / 2;
            const double expectedL = inside ? 2.0 * expectedLeft[index] : 0;
            const double expectedR = inside ? 2.0 * expectedRight[index] : 0;
            require(std::isfinite(left[index]) && std::isfinite(right[index]) &&
                        std::abs(left[index] - expectedL) < 1e-6 &&
                        std::abs(right[index] - expectedR) < 1e-6,
                    "ARA region synthesis lost crop, offset or overlapping sum");
            foundVoice = foundVoice || left[index] != 0 || right[index] != 0;
        }
    }
    require(foundVoice, "ARA region synthesis produced no voice");
    left.fill(0);
    right.fill(0);
    playback.mix(left.data(), right.data(), count, 2 * rate);
    reference.copyStereo(expectedLeft.data(), expectedRight.data(), count, rate / 4, rate);
    require(std::abs(left[0] - 2.0 * expectedLeft[0]) < 1e-6,
            "ARA region seek retained previous playback position");
    std::array<float, count> mono{};
    playback.mix(mono.data(), static_cast<float *>(nullptr), count, 2 * rate);
    for (int index = 0; index < count; ++index)
        require(std::abs(mono[index] - (expectedLeft[index] + expectedRight[index])) < 1e-6,
                "ARA mono region output did not average stereo channels");
    // One audio reader, as required by the VST3 processor contract, concurrent
    // with host-thread publication/reclamation and worker cancellation.
    std::atomic<bool> stop{false}, invalid{false};
    std::atomic<int> blocks{0};
    std::thread audio([&] {
        std::array<float, 512> audioLeft{}, audioRight{};
        while (!stop.load())
        {
            audioLeft.fill(0);
            audioRight.fill(0);
            playback.mix(audioLeft.data(), audioRight.data(), count, 2 * rate);
            for (int index = 0; index < count; ++index)
                if (!std::isfinite(audioLeft[index]) || !std::isfinite(audioRight[index]))
                    invalid.store(true);
            ++blocks;
        }
    });
    for (int revision = 0; revision < 12; ++revision)
    {
        playback.beginUpdate();
        playback.add(runtime, state, 0.25, revision % 2 == 0 ? 2 : 3, 0.5, false);
        playback.publish();
    }
    stop.store(true);
    audio.join();
    require(!invalid.load() && blocks.load() > 0, "Concurrent ARA region plan publication failed");
    playback.clear();
    std::cout << "PASS: concurrent ARA region plan replacement, audio blocks " << blocks.load()
              << std::endl;
    std::cout << "PASS: real SVS ARA region cache crop, offset, overlap, seek, float/mono and "
                 "double/stereo"
              << std::endl;
}

template <typename Sample>
void testEditorAudition(Document &document, ARAPlaybackRegionRef region, bool sequenceAssignment,
                        bool combined = false)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    auto factory = owned(GetPluginFactory());
    auto host = owned(new HostApplication());
    PClassInfo info{};
    require(factory->getClassInfo(0, &info) == kResultOk, "Audition component class unavailable");
    IComponent *raw = nullptr;
    require(factory->createInstance(info.cid, IComponent::iid, reinterpret_cast<void **>(&raw)) ==
                kResultOk,
            "Audition component creation failed");
    auto component = owned(raw);
    require(component->initialize(host) == kResultOk, "Audition initialization failed");
    FUnknownPtr<IAudioProcessor> processor(component);
    FUnknownPtr<ARA::IPlugInEntryPoint2> entry(component);
    const auto *bound = entry->bindToDocumentControllerWithRoles(
        document.ref(), kARAPlaybackRendererRole | kARAEditorRendererRole | kARAEditorViewRole,
        combined ? kARAPlaybackRendererRole | kARAEditorRendererRole : kARAEditorRendererRole);
    auto *model = PlugIn::fromRef<BridgePlaybackRegion>(region);
    auto owner = model->getAudioModification<BridgeAudioModification>()->editorDocument();
    const auto sequence = PlugIn::toRef(model->getRegionSequence());
    if (sequenceAssignment)
        bound->editorRendererInterface->addRegionSequence(bound->editorRendererRef, sequence);
    else
        bound->editorRendererInterface->addPlaybackRegion(bound->editorRendererRef, region);
    ProcessSetup setup{};
    setup.sampleRate = 48000;
    setup.maxSamplesPerBlock = 256;
    setup.symbolicSampleSize = std::is_same_v<Sample, float> ? kSample32 : kSample64;
    require(processor->setupProcessing(setup) == kResultOk &&
                component->setActive(true) == kResultOk,
            "Audition activation failed");
    processor->setProcessing(true);
    std::array<Sample, 256> inputLeft{}, inputRight{}, left{}, right{};
    inputLeft.fill(Sample(0.125));
    inputRight.fill(Sample(-0.125));
    Sample *inputPointers[]{inputLeft.data(), inputRight.data()};
    Sample *outputPointers[]{left.data(), right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    if constexpr (std::is_same_v<Sample, float>)
    {
        input.channelBuffers32 = inputPointers;
        output.channelBuffers32 = outputPointers;
    }
    else
    {
        input.channelBuffers64 = inputPointers;
        output.channelBuffers64 = outputPointers;
    }
    ProcessData data{};
    data.symbolicSampleSize = setup.symbolicSampleSize;
    data.numSamples = 256;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &input;
    data.outputs = &output;
    auto forwarded = [&] {
        return combined ? left == std::array<Sample, 256>{} && right == std::array<Sample, 256>{}
                        : left == inputLeft && right == inputRight;
    };
    require(processor->process(data) == kResultOk && forwarded(), "Idle audition altered input");
    auto playUntilAudible = [&] {
        EditorDocument::onKeyPreviewRequested(owner.get(), 60, 1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do
        {
            require(processor->process(data) == kResultOk, "Audition process failed");
            for (int index = 0; index < 256; ++index)
                require(std::isfinite(left[index]) && std::isfinite(right[index]),
                        "Audition produced nonfinite samples");
            if (!forwarded())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        require(false, "Assigned editor renderer produced no singing preview");
    };
    playUntilAudible();
    EditorDocument::onKeyPreviewRequested(owner.get(), -1, 0);
    require(processor->process(data) == kResultOk && forwarded(),
            "Released audition key kept sounding");
    for (int index = 0; index < 8; ++index)
        require(processor->process(data) == kResultOk && forwarded(),
                "Released audition revived from cache");
    playUntilAudible();
    EditorDocument::onKeyPreviewRequested(owner.get(), -1, 0);
    data.numOutputs = 0;
    require(processor->process(data) == kResultOk, "Audition no-output release flush failed");
    data.numOutputs = 1;
    require(processor->process(data) == kResultOk && forwarded(),
            "Flushed release retained audition");
    playUntilAudible();
    processor->setProcessing(false);
    processor->setProcessing(true);
    require(processor->process(data) == kResultOk && forwarded(),
            "Processing restart retained audition");
    playUntilAudible();
    if (sequenceAssignment)
        bound->editorRendererInterface->removeRegionSequence(bound->editorRendererRef, sequence);
    else
        bound->editorRendererInterface->removePlaybackRegion(bound->editorRendererRef, region);
    require(processor->process(data) == kResultOk && forwarded(),
            "Removing editor assignment retained audition");
    EditorDocument::onKeyPreviewRequested(owner.get(), 62, 1);
    require(processor->process(data) == kResultOk && forwarded(),
            "Unassigned document started audition");
    processor->setProcessing(false);
    require(component->setActive(false) == kResultOk && component->terminate() == kResultOk,
            "Audition cleanup failed");
    std::cout << "PASS: actual Bridge editor-only SVS audition, input forwarding, key release, "
                 "assignment removal, format "
              << setup.symbolicSampleSize << std::endl;
}

template <typename Sample>
void testRoleAudio(Document &document, ARAPlugInInstanceRoleFlags roles,
                   ARAPlaybackRegionRef region = nullptr)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    auto factory = owned(GetPluginFactory());
    auto host = owned(new HostApplication());
    PClassInfo info{};
    require(factory->getClassInfo(0, &info) == kResultOk, "Bridge class unavailable");
    IComponent *raw = nullptr;
    require(factory->createInstance(info.cid, IComponent::iid, reinterpret_cast<void **>(&raw)) ==
                kResultOk,
            "Bridge role component creation failed");
    auto component = owned(raw);
    require(component->initialize(host) == kResultOk, "Bridge role initialization failed");
    FUnknownPtr<IAudioProcessor> processor(component);
    FUnknownPtr<ARA::IPlugInEntryPoint2> entry(component);
    require(processor && entry, "Bridge role processing interfaces unavailable");
    const auto *bound = entry->bindToDocumentControllerWithRoles(
        document.ref(), kARAPlaybackRendererRole | kARAEditorRendererRole | kARAEditorViewRole,
        roles);
    if (region)
        bound->playbackRendererInterface->addPlaybackRegion(bound->playbackRendererRef, region);
    constexpr auto format = std::is_same_v<Sample, float> ? kSample32 : kSample64;
    ProcessSetup setup{};
    setup.processMode = region ? kOffline : kRealtime;
    setup.symbolicSampleSize = format;
    setup.maxSamplesPerBlock = 4;
    setup.sampleRate = 48000;
    require(processor->canProcessSampleSize(format) == kResultOk &&
                processor->setupProcessing(setup) == kResultOk &&
                component->setActive(true) == kResultOk,
            "Bridge role format activation failed");
    processor->setProcessing(true);
    std::array<Sample, 4> inputLeft{Sample(0.25), Sample(-0.5), Sample(0.75), Sample(0)};
    std::array<Sample, 4> inputRight{Sample(-0.25), Sample(0.5), Sample(0), Sample(1)};
    std::array<Sample, 4> outputLeft{}, outputRight{};
    Sample *inputs[]{inputLeft.data(), inputRight.data()};
    Sample *outputs[]{outputLeft.data(), outputRight.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    if constexpr (std::is_same_v<Sample, float>)
    {
        input.channelBuffers32 = inputs;
        output.channelBuffers32 = outputs;
    }
    else
    {
        input.channelBuffers64 = inputs;
        output.channelBuffers64 = outputs;
    }
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = format;
    data.numSamples = 4;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &input;
    data.outputs = &output;
    if (region)
    {
        ProcessContext context{};
        context.sampleRate = setup.sampleRate;
        context.state = ProcessContext::kPlaying;
        data.processMode = kOffline;
        data.processContext = &context;
        const auto *model = PlugIn::fromRef<BridgePlaybackRegion>(region);
        const auto start =
            static_cast<int64>(std::llround(model->getStartInPlaybackTime() * setup.sampleRate));
        const int notificationsBefore = document.regionUpdates;
        document.api()->notifyModelUpdates(document.ref());
        require(document.regionUpdates == notificationsBefore + 1,
                "Completed synthesis did not invalidate host region audio cache");
        document.api()->notifyModelUpdates(document.ref());
        require(document.regionUpdates == notificationsBefore + 1,
                "Completed synthesis notification repeated without a revision");
        context.projectTimeSamples = start - 4;
        require(processor->process(data) == kResultOk && outputLeft == std::array<Sample, 4>{} &&
                    outputRight == std::array<Sample, 4>{},
                "Assigned Bridge region leaked outside playback crop");
        bool audible = false;
        for (int offset = 0; offset < 4096; offset += 4)
        {
            context.projectTimeSamples = start + offset;
            require(processor->process(data) == kResultOk, "Assigned Bridge region process failed");
            for (int index = 0; index < 4; ++index)
            {
                require(std::isfinite(outputLeft[index]) && std::isfinite(outputRight[index]),
                        "Assigned Bridge region produced nonfinite audio");
                audible = audible || outputLeft[index] != 0 || outputRight[index] != 0;
            }
        }
        require(audible, "Assigned Bridge region produced no synthesized voice");
        context.projectTimeSamples =
            static_cast<int64>(std::llround(model->getEndInPlaybackTime() * setup.sampleRate));
        require(processor->process(data) == kResultOk && outputLeft == std::array<Sample, 4>{} &&
                    outputRight == std::array<Sample, 4>{},
                "Assigned Bridge region leaked after playback crop");
        ARAPlaybackRegionProperties moved{};
        moved.structSize = sizeof(moved);
        moved.startInModificationTime = model->getStartInAudioModificationTime();
        moved.durationInModificationTime = model->getDurationInAudioModificationTime();
        moved.startInPlaybackTime = model->getStartInPlaybackTime() + 3;
        moved.durationInPlaybackTime = model->getDurationInPlaybackTime();
        moved.regionSequenceRef = PlugIn::toRef(model->getRegionSequence());
        moved.transformationFlags = kARAPlaybackTransformationTimestretch;
        document.api()->beginEditing(document.ref());
        document.api()->updatePlaybackRegionProperties(document.ref(), region, &moved);
        document.api()->endEditing(document.ref());
        context.projectTimeSamples = start;
        require(processor->process(data) == kResultOk && outputLeft == std::array<Sample, 4>{} &&
                    outputRight == std::array<Sample, 4>{},
                "Active host edit retained old region placement");
        audible = false;
        std::vector<Sample> beforeEdit(4096);
        for (int offset = 0; offset < 4096; offset += 4)
        {
            context.projectTimeSamples = start + 3 * 48000 + offset;
            require(processor->process(data) == kResultOk, "Moved active region process failed");
            for (int index = 0; index < 4; ++index)
            {
                beforeEdit[offset + index] = outputLeft[index];
                audible = audible || outputLeft[index] != 0 || outputRight[index] != 0;
            }
        }
        require(audible, "Active host edit did not publish moved region synthesis");
        auto editingDocument =
            model->getAudioModification<BridgeAudioModification>()->editorDocument();
        const auto originalState = editingDocument->state;
        const auto originalHandle = editingDocument->handle;
        auto buildRenderState = editingDocument->runtime.buildAraRenderState;
        std::int32_t required = 0;
        require(buildRenderState(originalState.data(),
                                 static_cast<std::int32_t>(originalState.size()), 2, nullptr, 0,
                                 &required) == 0,
                "Edited snapshot size query failed");
        std::vector<std::uint8_t> editedState(required);
        require(
            buildRenderState(originalState.data(), static_cast<std::int32_t>(originalState.size()),
                             2, editedState.data(), required, &required) == 0 &&
                editingDocument->runtime.restoreDocumentState(originalHandle, editedState.data(),
                                                              required) == 0,
            "Edited managed snapshot preparation failed");
        // Exercise the real native callback boundary without constructing a GUI.
        EditorDocument::onStateChanged(editingDocument.get(), editedState.data(), required);
        double difference = 0;
        for (int offset = 0; offset < 4096; offset += 4)
        {
            context.projectTimeSamples = start + 3 * 48000 + offset;
            require(processor->process(data) == kResultOk, "Edited active region process failed");
            for (int index = 0; index < 4; ++index)
            {
                require(std::isfinite(outputLeft[index]), "Edited region audio is nonfinite");
                difference +=
                    std::abs(static_cast<double>(outputLeft[index] - beforeEdit[offset + index]));
            }
        }
        require(difference > 1e-6, "Settled editor snapshot did not refresh active Bridge audio");
        const int afterEditorNotifications = document.regionUpdates;
        document.api()->notifyModelUpdates(document.ref());
        require(document.regionUpdates == afterEditorNotifications + 1,
                "Edited synthesis completion did not invalidate host region audio cache");
        processor->setProcessing(false);
        require(component->setActive(false) == kResultOk, "Assigned Bridge deactivation failed");
        require(editingDocument->runtime.restoreDocumentState(
                    originalHandle, originalState.data(),
                    static_cast<std::int32_t>(originalState.size())) == 0,
                "Edited snapshot cleanup failed");
        EditorDocument::onStateChanged(editingDocument.get(), originalState.data(),
                                       static_cast<std::int32_t>(originalState.size()));
        moved.startInPlaybackTime -= 3;
        document.api()->beginEditing(document.ref());
        document.api()->updatePlaybackRegionProperties(document.ref(), region, &moved);
        document.api()->endEditing(document.ref());
        bound->playbackRendererInterface->removePlaybackRegion(bound->playbackRendererRef, region);
        require(component->terminate() == kResultOk, "Assigned Bridge termination failed");
        std::cout << "PASS: assigned actual Bridge component offline synthesis and crop, format "
                  << format << std::endl;
        return;
    }
    require(processor->process(data) == kResultOk, "Bridge role process failed");
    const bool editorOnly = (roles & kARAEditorRendererRole) && !(roles & kARAPlaybackRendererRole);
    if (editorOnly)
        require(outputLeft == inputLeft && outputRight == inputRight && output.silenceFlags == 0,
                "Editor-only rendering did not forward input audio");
    else
        require(outputLeft == std::array<Sample, 4>{} && outputRight == std::array<Sample, 4>{} &&
                    output.silenceFlags == 3,
                "Empty playback role leaked input audio");
    data.numInputs = 0;
    data.inputs = nullptr;
    require(processor->process(data) == kResultOk && outputLeft == std::array<Sample, 4>{} &&
                outputRight == std::array<Sample, 4>{} && output.silenceFlags == 3,
            "Bridge missing-input path did not produce silence");
    processor->setProcessing(false);
    require(component->setActive(false) == kResultOk && component->terminate() == kResultOk,
            "Bridge role processing cleanup failed");
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    try
    {
        require(argc == 2 || (argc == 3 && std::wstring(argv[2]) == L"--synthesis"),
                "Expected a TuneLab project state file and optional --synthesis");
        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        std::vector<std::uint8_t> state((std::istreambuf_iterator<char>(input)), {});
        require(!state.empty(), "State file is empty or missing");
        AraSourceIdentity savedSource{
            42,
            {{261.6256f, 60, 0.7f, 0.25, 0, 0.25, 0.3}, {261.6256f, 60, 0.9f, 0.25, 0, 0.5, 0.6}},
            {7, 41}};
        const auto identityBytes = encodeAraSourceIdentity(savedSource);
        const auto restoredSource = decodeAraSourceIdentity(identityBytes);
        require(restoredSource.nextId == 42 && restoredSource.ids == savedSource.ids &&
                    araNoteDifferenceGroups(restoredSource.notes[0], savedSource.notes[0]) == 0 &&
                    araNoteDifferenceGroups(restoredSource.notes[1], savedSource.notes[1]) == 0,
                "Source identity wire format lost counter, IDs or note fields");
        auto currentSourceNotes = restoredSource.notes;
        currentSourceNotes[1].startPosition = 0.5;
        currentSourceNotes.push_back({391.9954f, 67, 0.8f, 1, 0, 0.5, 0.5});
        auto restoredCounter = restoredSource.nextId;
        const auto recoveredIds = matchAraNoteIds(restoredSource.notes, restoredSource.ids,
                                                  currentSourceNotes, restoredCounter);
        require(recoveredIds == std::vector<std::uint64_t>{7, 41, 43},
                "Restored source snapshot did not preserve moved identity or allocator history");
        auto rejectsSource = [](const std::vector<std::uint8_t> &bytes) {
            try
            {
                decodeAraSourceIdentity(bytes);
            }
            catch (const std::invalid_argument &)
            {
                return true;
            }
            return false;
        };
        for (std::size_t length = 0; length < identityBytes.size(); ++length)
            require(rejectsSource({identityBytes.begin(), identityBytes.begin() + length}),
                    "Truncated source identity was accepted");
        auto corruptedIdentity = identityBytes;
        corruptedIdentity[8] = 99;
        require(rejectsSource(corruptedIdentity), "Unknown source matching version was accepted");
        corruptedIdentity = identityBytes;
        corruptedIdentity.push_back(0);
        require(rejectsSource(corruptedIdentity), "Trailing source identity byte was accepted");
        corruptedIdentity = identityBytes;
        corruptedIdentity[12] = 0;
        require(rejectsSource(corruptedIdentity), "Source counter below saved IDs was accepted");
        const auto bundleBytes = encodeAraArchiveBundle({{}, {{"source", identityBytes}}});
        require(decodeAraArchiveBundle(bundleBytes).sources.size() == 1 &&
                    decodeAraArchiveBundle(encodeAraArchive({{"old-mod", {}}})).sources.empty(),
                "ARA bundle source records or legacy compatibility failed");
        for (std::size_t length = 0; length < bundleBytes.size(); ++length)
        {
            bool rejected = false;
            try
            {
                decodeAraArchiveBundle({bundleBytes.begin(), bundleBytes.begin() + length});
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }
            require(rejected, "Truncated ARA archive bundle was accepted");
        }
        AraTimeline timeline({{10, 0}, {12, 4}, {16, 8}});
        require(timeline.quarterAtSeconds(11) == 2 && timeline.quarterAtSeconds(14) == 6 &&
                    timeline.quarterAtSeconds(9) == -2 && timeline.quarterAtSeconds(18) == 10 &&
                    timeline.secondsAtQuarter(0) == 10 && timeline.secondsAtQuarter(6) == 14,
                "ARA tempo mapping lost segment slope or song origin");
        for (double quarter = -8; quarter <= 20; quarter += 0.25)
            require(std::abs(timeline.quarterAtSeconds(timeline.secondsAtQuarter(quarter)) -
                             quarter) < 1e-10,
                    "ARA tempo map inverse round trip failed");
        ARAAssertFunction assertion = nullptr;
        ARAInterfaceConfiguration configuration{
            sizeof(configuration), getProbeFactory()->highestSupportedApiGeneration, &assertion};
        getProbeFactory()->initializeARAWithConfiguration(&configuration);
        struct FactoryScope
        {
            ~FactoryScope()
            {
                getProbeFactory()->uninitializeARA();
            }
        } factoryScope;
        {
            Document first;
            for (auto roles : std::array<ARAPlugInInstanceRoleFlags, 3>{
                     kARAPlaybackRendererRole, kARAEditorRendererRole,
                     kARAPlaybackRendererRole | kARAEditorRendererRole})
            {
                testRoleAudio<float>(first, roles);
                testRoleAudio<double>(first, roles);
            }
            auto *sourceModel = PlugIn::fromRef<BridgeAudioSource>(first.source);
            require(sourceModel->notesAvailable && sourceModel->notes.size() == 2 &&
                        sourceModel->notes[0].noteDuration == 0.25 &&
                        sourceModel->notes[1].noteDuration == 0.5 && first.contentReaders == 0,
                    "Source note reader lost duplicate-pitch/onset events or leaked");
            const auto revision = sourceModel->contentRevision;
            const auto originalNoteIds = sourceModel->noteIds;
            require(originalNoteIds.size() == 2 && originalNoteIds[0] != originalNoteIds[1],
                    "Duplicate-pitch/onset notes shared identity");
            first.api()->beginEditing(first.ref());
            first.api()->updateAudioSourceContent(first.ref(), first.source, nullptr, 0);
            first.api()->endEditing(first.ref());
            require(sourceModel->noteIds == originalNoteIds,
                    "Unchanged snapshot lost note identities");
            first.api()->beginEditing(first.ref());
            std::swap(first.sourceNotes[0], first.sourceNotes[1]);
            first.api()->updateAudioSourceContent(first.ref(), first.source, nullptr, 0);
            require(sourceModel->noteIds[0] == originalNoteIds[1] &&
                        sourceModel->noteIds[1] == originalNoteIds[0],
                    "Same-onset reordered events followed list index instead of full content");
            std::swap(first.sourceNotes[0], first.sourceNotes[1]);
            first.api()->updateAudioSourceContent(first.ref(), first.source, nullptr, 0);
            first.api()->endEditing(first.ref());
            auto duplicate = first.sourceNotes[0];
            auto movedDuplicate = duplicate;
            movedDuplicate.startPosition += 1;
            std::uint64_t nextIdentity = 2;
            const auto ambiguousIds = matchAraNoteIds(
                {duplicate, duplicate}, {1, 2}, {movedDuplicate, movedDuplicate}, nextIdentity);
            require(ambiguousIds[0] > 2 && ambiguousIds[1] > 2 &&
                        ambiguousIds[0] != ambiguousIds[1],
                    "Ambiguous duplicate moves inherited arbitrary old identities");
            first.api()->beginEditing(first.ref());
            first.sourceNotes.erase(first.sourceNotes.begin());
            first.sourceNotes[0].startPosition = 0.5;
            first.api()->updateAudioSourceContent(first.ref(), first.source, nullptr, 0);
            first.api()->endEditing(first.ref());
            require(sourceModel->notes.size() == 1 && sourceModel->notes[0].startPosition == 0.5 &&
                        sourceModel->contentRevision > revision && first.contentReaders == 0,
                    "Source note content update did not replace snapshot");
            require(sourceModel->noteIds[0] == originalNoteIds[1],
                    "Unambiguous moved note lost identity after duplicate deletion");
            first.api()->beginEditing(first.ref());
            first.sourceNotes.clear();
            first.api()->updateAudioSourceContent(first.ref(), first.source, nullptr, 0);
            require(sourceModel->notesAvailable && sourceModel->notes.empty(),
                    "Empty available note content was treated as unavailable");
            first.sourceNotesAvailable = false;
            first.api()->updateAudioSourceContent(first.ref(), first.source, nullptr, 0);
            first.api()->endEditing(first.ref());
            require(!sourceModel->notesAvailable && sourceModel->notes.empty() &&
                        !sourceModel->invalidNotes,
                    "Unavailable source note content retained stale notes");
            auto original = first.add("original");
            EditorDocument::onStateChanged(editor(original).get(), state.data(),
                                           static_cast<std::int32_t>(state.size()));
            EditorDocument::onStateChanged(editor(original).get(), state.data(),
                                           static_cast<std::int32_t>(state.size()));
            require(first.modificationUpdates.empty(),
                    "ARA content callback bypassed host polling");
            first.api()->notifyModelUpdates(first.ref());
            require(first.modificationUpdates.size() == 1 &&
                        first.modificationUpdates[0].wholeRange &&
                        first.modificationUpdates[0].modification ==
                            PlugIn::fromRef(original)->getHostRef() &&
                        first.modificationUpdates[0].flags == kARAContentUpdateEverythingChanged &&
                        first.sourceUpdates == 0 && first.regionUpdates == 0 &&
                        first.documentUpdates == 0,
                    "Settled modification edit notified the wrong host object or scope");
            first.api()->notifyModelUpdates(first.ref());
            require(first.modificationUpdates.size() == 1,
                    "ARA modification notification repeated without an edit");
            auto clone = first.add("copy", original);
            require(editor(original) != editor(clone), "Clone shares editor ownership");
            require(editor(clone)->state == state, "Clone lost project state");
            auto *originalModel = PlugIn::fromRef<BridgeAudioModification>(original);
            auto *cloneModel = PlugIn::fromRef<BridgeAudioModification>(clone);
            require(originalModel->pairEndpoint().id() != cloneModel->pairEndpoint().id() &&
                        originalModel->pairEndpoint().allowsProjectOutput() &&
                        cloneModel->pairEndpoint().allowsProjectOutput(),
                    "Modification clone reused a live pairing endpoint");
            editor(clone)->state.clear();
            require(editor(original)->state == state, "Clone mutated original state");
            {
                PairEndpoint consumer;
                auto document = editor(original);
                require(consumer.initialize(document->runtime, 1) &&
                            consumer.pairWith(originalModel->pairEndpoint().id().data()) == 0,
                        "Shared project consumer pairing failed");
                auto readShared = [&] {
                    std::int32_t size = 0;
                    require(consumer.copyDocumentState(nullptr, 0, &size) == 0,
                            "Shared project size query failed");
                    std::vector<std::uint8_t> bytes(size);
                    require(consumer.copyDocumentState(bytes.data(), size, &size) == 0,
                            "Shared project copy failed");
                    return bytes;
                };
                require(readShared() == state && document->initialize(),
                        "Shared project did not read the actual modification");
                std::int32_t wrongThread = 0;
                std::thread foreignThread([&] {
                    std::int32_t required = 0;
                    wrongThread = consumer.copyDocumentState(nullptr, 0, &required);
                });
                foreignThread.join();
                require(wrongThread == 4,
                        "Shared project read accessed ARA data from a foreign thread");
                std::int32_t size = 0;
                require(document->runtime.buildAraRenderState(
                            state.data(), static_cast<std::int32_t>(state.size()), 2, nullptr, 0,
                            &size) == 0,
                        "Shared edit preparation failed");
                std::vector<std::uint8_t> edited(size);
                require(document->runtime.buildAraRenderState(
                            state.data(), static_cast<std::int32_t>(state.size()), 2, edited.data(),
                            size, &size) == 0 &&
                            consumer.applyDocumentState(edited.data(), size) == 0,
                        "Shared project write failed");
                require(readShared() == edited && document->state == edited &&
                            editor(clone)->state.empty(),
                        "Shared project write missed its owner or changed the clone");
                std::int32_t managedSize = 0;
                require(document->runtime.copyDocumentState(document->handle, nullptr, 0,
                                                            &managedSize) == 0,
                        "Shared owner managed state query failed");
                std::vector<std::uint8_t> managedState(managedSize);
                require(document->runtime.copyDocumentState(document->handle, managedState.data(),
                                                            managedSize, &managedSize) == 0 &&
                            managedState == edited,
                        "Shared write updated native bytes but missed the real managed document");
                auto invalid = edited;
                invalid[16] = 0xFF;
                require(consumer.applyDocumentState(invalid.data(), size) == 1 &&
                            readShared() == edited,
                        "Invalid shared edit changed the owner");
                const auto restore = document->runtime.restoreAraDocumentState;
                document->runtime.restoreAraDocumentState = [](auto, auto, auto) -> std::int32_t {
                    return 7;
                };
                const auto busy = consumer.applyDocumentState(
                    state.data(), static_cast<std::int32_t>(state.size()));
                document->runtime.restoreAraDocumentState = restore;
                require(busy == 7 && readShared() == edited,
                        "Shared edit ignored the uncommitted-edit guard");
                require(consumer.applyDocumentState(state.data(),
                                                    static_cast<std::int32_t>(state.size())) == 0 &&
                            readShared() == state,
                        "Shared project cleanup failed");
                require(consumer.unpair() == 0, "Shared project unlink failed");
                size = 0;
                require(consumer.copyDocumentState(nullptr, 0, &size) == 2,
                        "Unlinked instrument retained shared document access");
            }

            Archive full;
            require(first.api()->storeObjectsToArchive(
                        first.ref(), reinterpret_cast<ARAArchiveWriterHostRef>(&full), nullptr),
                    "Full store failed");
            require(decodeAraArchiveBundle(full.bytes).modifications.size() == 2,
                    "Full archive omitted modification");
            require(decodeAraArchiveBundle(full.bytes).sources.size() == 1,
                    "Full archive omitted source identity");
            ARAStoreObjectsFilter store{};
            store.structSize = sizeof(store);
            store.audioModificationRefsCount = 1;
            store.audioModificationRefs = &original;
            Archive partial;
            require(first.api()->storeObjectsToArchive(
                        first.ref(), reinterpret_cast<ARAArchiveWriterHostRef>(&partial), &store),
                    "Partial store failed");
            require(decodeAraArchiveBundle(partial.bytes).modifications.size() == 1,
                    "Partial store ignored filter");
            require(decodeAraArchiveBundle(partial.bytes).sources.empty(),
                    "Modification-only archive stored an unrequested source");

            Document second;
            auto *secondSourceModel = PlugIn::fromRef<BridgeAudioSource>(second.source);
            secondSourceModel->noteIds = {7, 41};
            secondSourceModel->nextNoteId = 42;
            auto renamed = second.add("renamed");
            auto unaffected = second.add("unaffected");
            const ARAPersistentID archivedIDs[]{"original", "copy"};
            const ARAPersistentID currentIDs[]{"renamed", "unaffected"};
            ARARestoreObjectsFilter restore{};
            restore.structSize = sizeof(restore);
            restore.audioModificationIDsCount = 1;
            restore.audioModificationArchiveIDs = archivedIDs;
            restore.audioModificationCurrentIDs = currentIDs;
            require(second.restore(partial, restore), "Partial mapped restore failed");
            require(editor(renamed)->state == state && editor(unaffected)->state.empty(),
                    "Partial restore changed wrong objects");
            editor(renamed)->state.clear();
            editor(unaffected)->state = state;
            restore.audioModificationIDsCount = 2;
            require(second.restore(full, restore), "Full mapped restore failed");
            require(editor(renamed)->state == state && editor(unaffected)->state.empty(),
                    "Full restore lost mapped states");
            Archive invalid{encodeAraArchive({{"original", {}}, {"copy", {0xff}}})};
            require(!second.restore(invalid, restore), "Invalid CBOR was accepted");
            require(editor(renamed)->state == state && editor(unaffected)->state.empty(),
                    "Failed restore partially mutated documents");

            require(editor(renamed)->initialize() && editor(unaffected)->initialize(),
                    "Managed document activation failed");
            Archive swap{encodeAraArchive({{"original", {}}, {"copy", state}})};
            require(second.restore(swap, restore), "Live managed document restore failed");
            auto readManagedState = [](const std::shared_ptr<EditorDocument> &document) {
                std::int32_t required = 0;
                require(document->runtime.copyDocumentState(document->handle, nullptr, 0,
                                                            &required) == 0,
                        "Managed state size query failed");
                std::vector<std::uint8_t> bytes(required);
                require(document->runtime.copyDocumentState(document->handle, bytes.data(),
                                                            required, &required) == 0,
                        "Managed state copy failed");
                return bytes;
            };
            require(readManagedState(editor(unaffected)) == state,
                    "Live managed document did not receive restored project");
            second.api()->notifyModelUpdates(second.ref());
            require(second.modificationUpdates.empty() && second.sourceUpdates == 0 &&
                        second.regionUpdates == 0 && second.documentUpdates == 0,
                    "Archive restore echoed plugin-originated content notifications");
            if (argc == 3)
                testRegionSynthesis(editor(unaffected)->runtime, state);
            const auto emptyState = readManagedState(editor(renamed));
            require(emptyState != state, "Empty state did not reset live managed document");
            require(!second.restore(invalid, restore), "Live restore accepted invalid CBOR");
            require(readManagedState(editor(renamed)) == emptyState &&
                        readManagedState(editor(unaffected)) == state,
                    "Invalid live restore mutated managed documents");
            // Inject a failure only at the private ABI call boundary. The
            // preceding document really changes and must really roll back.
            originalRestore = editor(unaffected)->runtime.restoreDocumentState;
            editor(unaffected)->runtime.restoreDocumentState = failOnceRestore;
            rejectNextRestore = true;
            require(!second.restore(full, restore), "Injected restore failure was ignored");
            editor(unaffected)->runtime.restoreDocumentState = originalRestore;
            require(readManagedState(editor(renamed)) == emptyState &&
                        readManagedState(editor(unaffected)) == state &&
                        editor(renamed)->state.empty() && editor(unaffected)->state == state,
                    "Failed live restore did not roll back managed and native states");

            // Protocol-only editor role; setEditorOpen does not create a GUI.
            PlugIn::PlugInExtension extension;
            extension.bindToARA(second.ref(), kARAEditorViewRole, kARAEditorViewRole);
            auto *view = extension.getEditorView<BridgeEditorView>();
            require(view != nullptr, "Editor view role was not created");
            std::shared_ptr<EditorDocument> notifiedSelection;
            view->selectionChanged = [&](auto selected) {
                notifiedSelection = std::move(selected);
            };
            view->setOpen(true);
            require(!view->selectedDocument(), "Empty selection resolved a document");
            second.api()->beginEditing(second.ref());
            ARAMusicalContextProperties contextProperties{};
            contextProperties.structSize = sizeof(contextProperties);
            auto context = second.api()->createMusicalContext(
                second.ref(), reinterpret_cast<ARAMusicalContextHostRef>(&second.tokens[0]),
                &contextProperties);
            auto *contextModel = PlugIn::fromRef<BridgeMusicalContext>(context);
            require(contextModel->timeline && contextModel->timeline->quarterAtSeconds(14) == 6 &&
                        second.contentReaders == 0,
                    "Host musical context tempo was not read or reader leaked");
            require(contextModel->timeline->barSignatures().size() == 2 &&
                        contextModel->timeline->barSignatures()[0].position == -4 &&
                        contextModel->timeline->barSignatures()[1].numerator == 3,
                    "Host bar signature origin or meter was lost");
            second.tempoPoints = {{10, 0}, {14, 4}, {16, 8}};
            second.signatures[1].numerator = 7;
            second.signatures[1].denominator = 8;
            second.api()->updateMusicalContextContent(second.ref(), context, nullptr, 0);
            require(contextModel->timeline && contextModel->timeline->quarterAtSeconds(14) == 4 &&
                        second.contentReaders == 0,
                    "Musical context update did not replace tempo sync points");
            require(contextModel->timeline->barSignatures()[1].numerator == 7 &&
                        contextModel->timeline->barSignatures()[1].denominator == 8,
                    "Host meter update retained old signature");
            ARARegionSequenceProperties sequenceProperties{};
            sequenceProperties.structSize = sizeof(sequenceProperties);
            sequenceProperties.musicalContextRef = context;
            auto sequence = second.api()->createRegionSequence(
                second.ref(), reinterpret_cast<ARARegionSequenceHostRef>(&second.tokens[0]),
                &sequenceProperties);
            ARAPlaybackRegionProperties regionProperties{};
            regionProperties.structSize = sizeof(regionProperties);
            regionProperties.durationInModificationTime = 1;
            regionProperties.durationInPlaybackTime = 1;
            regionProperties.regionSequenceRef = sequence;
            auto region = second.api()->createPlaybackRegion(
                second.ref(), renamed,
                reinterpret_cast<ARAPlaybackRegionHostRef>(&second.tokens[0]), &regionProperties);
            auto *regionModel = PlugIn::fromRef<BridgePlaybackRegion>(region);
            regionProperties.startInModificationTime = 0.25;
            regionProperties.durationInModificationTime = 0.5;
            regionProperties.startInPlaybackTime = 14;
            regionProperties.durationInPlaybackTime = 0.5;
            second.api()->updatePlaybackRegionProperties(second.ref(), region, &regionProperties);
            require(regionModel->modificationSecondsAtSongSeconds(14.25) == 0.5 &&
                        !regionModel->modificationSecondsAtSongSeconds(13.99) &&
                        !regionModel->modificationSecondsAtSongSeconds(14.5) &&
                        regionModel->songQuarterAtModificationSeconds(0.25) == 4 &&
                        regionModel->songQuarterAtModificationSeconds(0.75) == 5,
                    "Region crop/source/playback offsets were not mapped through host tempo");
            regionProperties.startInPlaybackTime = 10;
            second.api()->updatePlaybackRegionProperties(second.ref(), region, &regionProperties);
            require(regionModel->songQuarterAtModificationSeconds(0.25) == 0 &&
                        regionModel->modificationSecondsAtSongSeconds(10.25) == 0.5,
                    "Moving host region retained stale coordinates");
            regionProperties.durationInModificationTime = 0;
            regionProperties.durationInPlaybackTime = 0;
            second.api()->updatePlaybackRegionProperties(second.ref(), region, &regionProperties);
            require(!regionModel->modificationSecondsAtSongSeconds(10) &&
                        !regionModel->songQuarterAtModificationSeconds(0.25),
                    "Zero-length host region produced a time mapping");
            regionProperties.durationInModificationTime = 0.5;
            regionProperties.durationInPlaybackTime = 0.5;
            second.api()->updatePlaybackRegionProperties(second.ref(), region, &regionProperties);
            second.api()->endEditing(second.ref());
            ARAViewSelection selection{};
            selection.structSize = sizeof(selection);
            selection.playbackRegionRefsCount = 1;
            selection.playbackRegionRefs = &region;
            const auto stateBeforeProjection = editor(renamed)->state;
            bool retainedRetry = false;
            editor(renamed)->retryPendingHostUpdate = [&] { retainedRetry = true; };
            const auto projectedState = regionModel->projectState();
            require(projectedState && !projectedState->empty() &&
                        editor(renamed)->state == stateBeforeProjection,
                    "Region projection replaced the shared editing document");
            require(static_cast<bool>(editor(renamed)->retryPendingHostUpdate),
                    "Region projection cleared a pending editor update");
            editor(renamed)->retryPendingHostUpdate();
            require(retainedRetry, "Region projection replaced the pending retry callback");
            editor(renamed)->retryPendingHostUpdate = {};
            view->notifySelection(&selection);
            require(view->selectedDocument() == editor(renamed),
                    "Selection resolved wrong document");
            const auto importedState = editor(renamed)->state;
            require(!importedState.empty() && importedState != emptyState &&
                        editor(renamed)->runtime.validateProjectState(
                            importedState.data(),
                            static_cast<std::int32_t>(importedState.size())) == 0 &&
                        readManagedState(editor(renamed)) == importedState,
                    "Initial source import did not reach native and managed project state");
            view->selectedDocument();
            require(editor(renamed)->state == importedState,
                    "Repeated selection overwrote imported editing state");
            require(importedState[8] == 2, "Initial import did not retain ARA metadata envelope");
            Archive importedArchive;
            require(second.api()->storeObjectsToArchive(
                        second.ref(), reinterpret_cast<ARAArchiveWriterHostRef>(&importedArchive),
                        nullptr),
                    "ARA metadata archive store failed");
            {
                Document reopened("reopened-source");
                auto reopenedOriginal = reopened.add("reopened-original");
                reopened.add("reopened-other");
                auto *reopenedSource = PlugIn::fromRef<BridgeAudioSource>(reopened.source);
                reopened.api()->beginEditing(reopened.ref());
                reopened.sourceNotes[1].startPosition = 0.5;
                reopened.sourceNotes.push_back({391.9954f, 67, 0.8f, 1, 0, 0.5, 0.5});
                reopened.api()->updateAudioSourceContent(reopened.ref(), reopened.source, nullptr,
                                                         0);
                reopened.api()->endEditing(reopened.ref());
                const ARAPersistentID archivedSources[]{"source"};
                const ARAPersistentID currentSources[]{"reopened-source"};
                const ARAPersistentID reopenedIds[]{"reopened-original", "reopened-other"};
                ARARestoreObjectsFilter reopenedRestore{};
                reopenedRestore.structSize = sizeof(reopenedRestore);
                reopenedRestore.audioSourceIDsCount = 1;
                reopenedRestore.audioSourceArchiveIDs = archivedSources;
                reopenedRestore.audioSourceCurrentIDs = currentSources;
                reopenedRestore.audioModificationIDsCount = 2;
                reopenedRestore.audioModificationArchiveIDs = currentIDs;
                reopenedRestore.audioModificationCurrentIDs = reopenedIds;
                require(reopened.restore(importedArchive, reopenedRestore),
                        "Reopened source restore failed");
                require(reopenedSource->noteIds == std::vector<std::uint64_t>{7, 41, 43} &&
                            reopenedSource->nextNoteId == 43 && reopenedSource->notes.size() == 3 &&
                            reopenedSource->notes[1].startPosition == 0.5 &&
                            reopenedSource->getPersistentID() == "reopened-source" &&
                            editor(reopenedOriginal)->state == importedState,
                        "Source restore lost identity mapping or overwrote current host content");
                require(reopened.restore(importedArchive, reopenedRestore) &&
                            reopenedSource->noteIds == std::vector<std::uint64_t>{7, 41, 43} &&
                            reopenedSource->nextNoteId == 43,
                        "Repeated restore changed post-archive host note identities");
                reopened.api()->beginEditing(reopened.ref());
                ARAMusicalContextProperties restoredContextProperties{};
                restoredContextProperties.structSize = sizeof(restoredContextProperties);
                auto restoredContext = reopened.api()->createMusicalContext(
                    reopened.ref(), reinterpret_cast<ARAMusicalContextHostRef>(&reopened.tokens[0]),
                    &restoredContextProperties);
                ARARegionSequenceProperties restoredSequenceProperties{};
                restoredSequenceProperties.structSize = sizeof(restoredSequenceProperties);
                restoredSequenceProperties.musicalContextRef = restoredContext;
                auto restoredSequence = reopened.api()->createRegionSequence(
                    reopened.ref(), reinterpret_cast<ARARegionSequenceHostRef>(&reopened.tokens[0]),
                    &restoredSequenceProperties);
                ARAPlaybackRegionProperties restoredRegionProperties{};
                restoredRegionProperties.structSize = sizeof(restoredRegionProperties);
                restoredRegionProperties.regionSequenceRef = restoredSequence;
                restoredRegionProperties.startInPlaybackTime = 10;
                restoredRegionProperties.durationInModificationTime = 2;
                restoredRegionProperties.durationInPlaybackTime = 2;
                auto restoredRegion = reopened.api()->createPlaybackRegion(
                    reopened.ref(), reopenedOriginal,
                    reinterpret_cast<ARAPlaybackRegionHostRef>(&reopened.tokens[0]),
                    &restoredRegionProperties);
                reopened.api()->endEditing(reopened.ref());
                auto *restoredRegionModel = PlugIn::fromRef<BridgePlaybackRegion>(restoredRegion);
                require(restoredRegionModel->synchronizeScore() &&
                            editor(reopenedOriginal)->initialize(),
                        "Reopened source could not synchronize with its saved association");
                const auto recoveredScore = editor(reopenedOriginal)->state;
                require(recoveredScore != importedState &&
                            readManagedState(editor(reopenedOriginal)) == recoveredScore,
                        "Reopened source changes did not reach the new managed document");
                reopened.api()->beginEditing(reopened.ref());
                reopened.sourceNotes[0].pitchNumber = 64;
                reopened.sourceNotes[0].frequency = 329.6276f;
                reopened.api()->updateAudioSourceContent(reopened.ref(), reopened.source, nullptr,
                                                         0);
                reopened.api()->endEditing(reopened.ref());
                require(restoredRegionModel->synchronizeScore() &&
                            editor(reopenedOriginal)->state != recoveredScore &&
                            readManagedState(editor(reopenedOriginal)) ==
                                editor(reopenedOriginal)->state &&
                            reopenedSource->noteIds == std::vector<std::uint64_t>{7, 41, 43},
                        "Post-reopen source update lost IDs or failed to update managed state");
                auto invalidBundle = decodeAraArchiveBundle(importedArchive.bytes);
                invalidBundle.modifications[0].state = {0xff};
                Archive invalidSourceTransaction{encodeAraArchiveBundle(invalidBundle)};
                require(!reopened.restore(invalidSourceTransaction, reopenedRestore) &&
                            reopenedSource->noteIds == std::vector<std::uint64_t>{7, 41, 43} &&
                            reopenedSource->nextNoteId == 43,
                        "Invalid modification partially restored source identity");
                reopenedSource->noteIds = {107, 141, 143};
                reopenedSource->nextNoteId = 200;
                reopenedRestore.audioSourceIDsCount = 0;
                reopenedRestore.audioSourceArchiveIDs = nullptr;
                reopenedRestore.audioSourceCurrentIDs = nullptr;
                require(reopened.restore(importedArchive, reopenedRestore) &&
                            reopenedSource->noteIds == std::vector<std::uint64_t>{107, 141, 143} &&
                            reopenedSource->nextNoteId == 200,
                        "Modification-only restore mutated an unrequested source");
            }
            ARARestoreObjectsFilter importedRestore{};
            importedRestore.structSize = sizeof(importedRestore);
            importedRestore.audioModificationIDsCount = 2;
            importedRestore.audioModificationArchiveIDs = currentIDs;
            importedRestore.audioModificationCurrentIDs = currentIDs;
            editor(renamed)->state.clear();
            require(second.restore(importedArchive, importedRestore) &&
                        editor(renamed)->state == importedState &&
                        readManagedState(editor(renamed)) == importedState,
                    "ARA archive lost v2 association metadata or managed document state");
            second.api()->beginEditing(second.ref());
            second.sourceNotes[0].pitchNumber = 64;
            second.sourceNotes[0].frequency = 329.6276f;
            second.api()->updateAudioSourceContent(second.ref(), second.source, nullptr, 0);
            second.api()->endEditing(second.ref());
            const auto updatedSourceState = editor(renamed)->state;
            require(updatedSourceState != importedState &&
                        readManagedState(editor(renamed)) == updatedSourceState,
                    "Host source update did not reach the associated managed project");
            second.api()->beginEditing(second.ref());
            second.api()->updateAudioSourceContent(second.ref(), second.source, nullptr, 0);
            second.api()->endEditing(second.ref());
            require(editor(renamed)->state == updatedSourceState,
                    "Repeated host source content update changed the saved score");
            originalAraRestore = editor(renamed)->runtime.restoreAraDocumentState;
            editor(renamed)->runtime.restoreAraDocumentState = busyOnceAraRestore;
            busyNextAraRestore = true;
            second.api()->beginEditing(second.ref());
            second.sourceNotes[0].pitchNumber = 67;
            second.sourceNotes[0].frequency = 391.9954f;
            second.api()->updateAudioSourceContent(second.ref(), second.source, nullptr, 0);
            second.api()->endEditing(second.ref());
            require(editor(renamed)->state == updatedSourceState &&
                        bool(editor(renamed)->retryPendingHostUpdate),
                    "Busy source update replaced state or failed to register retry");
            editor(renamed)->runtime.restoreAraDocumentState = originalAraRestore;
            EditorDocument::onPositionRequested(editor(renamed).get());
            require(!editor(renamed)->retryPendingHostUpdate &&
                        editor(renamed)->state != updatedSourceState &&
                        readManagedState(editor(renamed)) == editor(renamed)->state,
                    "Deferred source update did not complete from editor polling");
            require(notifiedSelection == editor(renamed), "Selection callback missed document");
            if (argc == 3)
            {
                second.api()->beginEditing(second.ref());
                auto audioRegionProperties = regionProperties;
                audioRegionProperties.startInModificationTime = 0.25;
                audioRegionProperties.startInPlaybackTime = 2;
                audioRegionProperties.durationInModificationTime = 0.5;
                audioRegionProperties.durationInPlaybackTime = 0.5;
                auto audioRegion = second.api()->createPlaybackRegion(
                    second.ref(), unaffected,
                    reinterpret_cast<ARAPlaybackRegionHostRef>(&second.tokens[0]),
                    &audioRegionProperties);
                second.api()->endEditing(second.ref());
                testEditorAudition<float>(second, audioRegion, false);
                testEditorAudition<double>(second, audioRegion, true);
                testEditorAudition<double>(second, audioRegion, false, true);
                testRoleAudio<float>(second, kARAPlaybackRendererRole, audioRegion);
                testRoleAudio<double>(second, kARAPlaybackRendererRole | kARAEditorRendererRole,
                                      audioRegion);
                for (double scale : {0.5, 2.0})
                {
                    second.api()->beginEditing(second.ref());
                    audioRegionProperties.durationInPlaybackTime = 0.5 * scale;
                    audioRegionProperties.transformationFlags =
                        kARAPlaybackTransformationTimestretch;
                    second.api()->updatePlaybackRegionProperties(second.ref(), audioRegion,
                                                                 &audioRegionProperties);
                    second.api()->endEditing(second.ref());
                    testRoleAudio<float>(second, kARAPlaybackRendererRole, audioRegion);
                    testRoleAudio<double>(second, kARAPlaybackRendererRole | kARAEditorRendererRole,
                                          audioRegion);
                }
                second.api()->beginEditing(second.ref());
                second.api()->destroyPlaybackRegion(second.ref(), audioRegion);
                second.api()->endEditing(second.ref());
            }
            testProxyBinding(second, region, renamed, false);
            testProxyBinding(second, region, renamed, true);
            editor(renamed)->runtime.restoreAraDocumentState = busyOnceAraRestore;
            busyNextAraRestore = true;
            second.api()->beginEditing(second.ref());
            second.sourceNotes[0].pitchNumber = 70;
            second.sourceNotes[0].frequency = 466.1638f;
            second.api()->updateAudioSourceContent(second.ref(), second.source, nullptr, 0);
            second.api()->endEditing(second.ref());
            require(bool(editor(renamed)->retryPendingHostUpdate),
                    "Deletion fixture did not defer update");
            editor(renamed)->runtime.restoreAraDocumentState = originalAraRestore;
            const auto stateBeforeRegionDeletion = editor(renamed)->state;
            second.api()->beginEditing(second.ref());
            second.api()->destroyPlaybackRegion(second.ref(), region);
            second.api()->destroyRegionSequence(second.ref(), sequence);
            second.api()->destroyMusicalContext(second.ref(), context);
            second.api()->endEditing(second.ref());
            require(!view->selectedDocument(), "Destroyed region left a stale selection");
            require(!notifiedSelection, "Editing completion did not clear deleted selection");
            EditorDocument::onPositionRequested(editor(renamed).get());
            require(!editor(renamed)->retryPendingHostUpdate &&
                        editor(renamed)->state == stateBeforeRegionDeletion,
                    "Deferred update dereferenced a destroyed region or changed its old document");
            view->setOpen(false);
        }
        std::cout << "PASS: nonempty ARA archive, deep clone, partial restore, ID remapping, "
                     "transactional rejection, live rollback, actual Bridge ConnectionProxy "
                     "selection in both connection orders and disconnect cleanup (no GUI)\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
