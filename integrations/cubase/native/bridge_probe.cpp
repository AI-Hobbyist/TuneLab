#include "ARA_API/ARAVST3.h"
#include "ara_document.h"
#include "ara_playback.h"
#include "editor_controller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "probe_state.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

DEF_CLASS_IID(ARA::IMainFactory)
DEF_CLASS_IID(ARA::IPlugInEntryPoint)
DEF_CLASS_IID(ARA::IPlugInEntryPoint2)

namespace
{
const FUID processorId(0x37B90B35, 0x0C7E44BA, 0x8BC26A10, 0xDF6A75B1);
const FUID controllerId(0x90B2D537, 0xC5F04A63, 0x91EE4E25, 0xB43F829A);
const FUID factoryId(0xFE5C28B4, 0x9A6342C8, 0xB1D0FA52, 0x68B3C901);

class BridgeProbe final : public AudioEffect,
                          public ARA::IPlugInEntryPoint,
                          public ARA::IPlugInEntryPoint2
{
  public:
    BridgeProbe()
    {
        setControllerClass(controllerId);
        processContextRequirements.needTransportState();
    }
    ~BridgeProbe() override
    {
        hostEdits.reset();
        modelPolls.reset();
        if (auto *view = extension.getEditorView<TuneLab::Cubase::BridgeEditorView>())
            view->selectionChanged = {};
    }

    tresult PLUGIN_API connect(IConnectionPoint *other) override
    {
        const auto result = AudioEffect::connect(other);
        if (result == kResultOk && extension.isBoundToARA())
            publishSelection();
        return result;
    }

    tresult PLUGIN_API notify(IMessage *message) override
    {
        if (message && message->getMessageID() &&
            std::strcmp(message->getMessageID(), "TuneLab.ARA.EditorOpen") == 0)
        {
            int64 value = 0;
            if (message->getAttributes()->getInt("open", value) != kResultOk)
                return kInvalidArgument;
            editorOpen = value != 0;
            if (auto *view = extension.getEditorView<TuneLab::Cubase::BridgeEditorView>())
                view->setOpen(editorOpen);
            return kResultOk;
        }
        return AudioEffect::notify(message);
    }

    tresult PLUGIN_API disconnect(IConnectionPoint *other) override
    {
        if (other == getPeer())
        {
            editorOpen = false;
            if (auto *view = extension.getEditorView<TuneLab::Cubase::BridgeEditorView>())
                view->setOpen(false);
            binding.select(nullptr);
        }
        return AudioEffect::disconnect(other);
    }

    static FUnknown *create(void *)
    {
        return static_cast<IAudioProcessor *>(new BridgeProbe());
    }

    tresult PLUGIN_API initialize(FUnknown *context) override
    {
        const auto result = AudioEffect::initialize(context);
        if (result != kResultOk)
        {
            return result;
        }
        addAudioInput(STR16("Stereo Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Output"), SpeakerArr::kStereo);
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool active) override
    {
        if (!active)
        {
            renderingActive = false;
            const auto result = AudioEffect::setActive(active);
            playback.clear();
            if (auto *editor = extension.getEditorRenderer<TuneLab::Cubase::BridgeEditorRenderer>())
                editor->setActive(false, 0, 0);
            return result;
        }
        if (rebuildPlayback(true) != kResultOk)
            return kResultFalse;
        if (auto *editor = extension.getEditorRenderer<TuneLab::Cubase::BridgeEditorRenderer>())
            editor->setActive(true, processSetup.maxSamplesPerBlock,
                              static_cast<int>(processSetup.sampleRate));
        const auto result = AudioEffect::setActive(active);
        renderingActive = result == kResultOk;
        return result;
    }

    tresult rebuildPlayback(bool initial)
    {
        completionNotified = false;
        try
        {
            if (initial)
                playback.prepare(processSetup.maxSamplesPerBlock,
                                 static_cast<std::int32_t>(processSetup.sampleRate));
            else
                playback.beginUpdate();
            if (auto *renderer = extension.getPlaybackRenderer())
            {
                for (auto *region :
                     renderer->getPlaybackRegions<TuneLab::Cubase::BridgePlaybackRegion>())
                {
                    if (region->getDurationInPlaybackTime() <= 0)
                        continue;
                    auto state = region->projectState();
                    if (!state)
                        throw std::runtime_error("ARA region score is unavailable");
                    auto document =
                        region->getAudioModification<TuneLab::Cubase::BridgeAudioModification>()
                            ->editorDocument();
                    document->observeStateChanges(hostEdits);
                    if (!document->runtime.renderSnapshot &&
                        !document->runtime.initialize(document->managedDirectory))
                        throw std::runtime_error("ARA synthesis runtime initialization failed");
                    const double scale = region->getDurationInPlaybackTime() /
                                         region->getDurationInAudioModificationTime();
                    std::int32_t required = 0;
                    auto build = document->runtime.buildAraRenderState;
                    const auto size = static_cast<std::int32_t>(state->size());
                    if (build(state->data(), size, scale, nullptr, 0, &required) != 0)
                        throw std::runtime_error("ARA render time mapping failed");
                    std::vector<std::uint8_t> renderState(required);
                    if (build(state->data(), size, scale, renderState.data(), required,
                              &required) != 0)
                        throw std::runtime_error("ARA render state copy failed");
                    playback.add(document->runtime, std::move(renderState),
                                 region->getStartInAudioModificationTime() * scale,
                                 region->getStartInPlaybackTime(),
                                 region->getDurationInPlaybackTime(),
                                 processSetup.processMode == kOffline, region->reference().lock());
                }
            }
            playback.publish();
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            playback.clear();
            return kResultFalse;
        }
        return kResultOk;
    }

    tresult PLUGIN_API process(ProcessData &data) override
    {
        hostTime->publish(data.processContext);
        // ARA editor-only instances must forward their input. Playback roles
        // replace input with their prepared region synthesis caches.
        const bool forwardInput = extension.getEditorRenderer() && !extension.getPlaybackRenderer();
        for (int32 bus = 0; bus < data.numOutputs; ++bus)
        {
            auto &output = data.outputs[bus];
            const auto *input = forwardInput && bus < data.numInputs ? &data.inputs[bus] : nullptr;
            output.silenceFlags = 0;
            for (int32 channel = 0; channel < output.numChannels; ++channel)
            {
                const bool hasInput = input && channel < input->numChannels;
                if (data.symbolicSampleSize == kSample32)
                {
                    if (hasInput)
                        std::copy_n(input->channelBuffers32[channel], data.numSamples,
                                    output.channelBuffers32[channel]);
                    else
                        std::fill_n(output.channelBuffers32[channel], data.numSamples, 0.0f);
                }
                else
                {
                    if (hasInput)
                        std::copy_n(input->channelBuffers64[channel], data.numSamples,
                                    output.channelBuffers64[channel]);
                    else
                        std::fill_n(output.channelBuffers64[channel], data.numSamples, 0.0);
                }
                if (!hasInput || (input->silenceFlags & (uint64(1) << channel)))
                    output.silenceFlags |= uint64(1) << channel;
            }
        }
        if (data.numOutputs > 0 && data.outputs[0].numChannels > 0 && data.processContext &&
            (data.processMode == kOffline ||
             (data.processContext->state & ProcessContext::kPlaying)))
        {
            auto &output = data.outputs[0];
            bool audible = false;
            if (data.symbolicSampleSize == kSample32)
                audible =
                    playback.mix(output.channelBuffers32[0],
                                 output.numChannels > 1 ? output.channelBuffers32[1] : nullptr,
                                 data.numSamples, data.processContext->projectTimeSamples);
            else
                audible =
                    playback.mix(output.channelBuffers64[0],
                                 output.numChannels > 1 ? output.channelBuffers64[1] : nullptr,
                                 data.numSamples, data.processContext->projectTimeSamples);
            if (audible)
                output.silenceFlags &= ~uint64(3);
        }
        if (auto *editor = extension.getEditorRenderer<TuneLab::Cubase::BridgeEditorRenderer>())
        {
            bool audible = false;
            if (data.numOutputs == 0 || data.outputs[0].numChannels == 0)
                editor->preview.mix<float>(nullptr, nullptr, data.numSamples);
            else
            {
                auto &output = data.outputs[0];
                if (data.symbolicSampleSize == kSample32)
                    audible = editor->preview.mix(
                        output.channelBuffers32[0],
                        output.numChannels > 1 ? output.channelBuffers32[1] : nullptr,
                        data.numSamples);
                else
                    audible = editor->preview.mix(
                        output.channelBuffers64[0],
                        output.numChannels > 1 ? output.channelBuffers64[1] : nullptr,
                        data.numSamples);
                if (audible)
                    output.silenceFlags &= ~uint64(3);
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setProcessing(TBool enabled) override
    {
        if (!enabled)
            if (auto *editor = extension.getEditorRenderer<TuneLab::Cubase::BridgeEditorRenderer>())
                editor->preview.stop();
        return kResultOk;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API getState(IBStream *stream) override
    {
        return TuneLab::Cubase::writeProbeState(stream);
    }

    tresult PLUGIN_API setState(IBStream *stream) override
    {
        return TuneLab::Cubase::readProbeState(stream);
    }

    const ARA::ARAFactory *PLUGIN_API getFactory() override
    {
        return TuneLab::Cubase::getProbeFactory();
    }

    const ARA::ARAPlugInExtensionInstance *PLUGIN_API
    bindToDocumentController(ARA::ARADocumentControllerRef) override
    {
        return nullptr; // ARA 1 binding is unsupported.
    }

    const ARA::ARAPlugInExtensionInstance *PLUGIN_API bindToDocumentControllerWithRoles(
        ARA::ARADocumentControllerRef document, ARA::ARAPlugInInstanceRoleFlags knownRoles,
        ARA::ARAPlugInInstanceRoleFlags assignedRoles) override
    {
        const auto *result = extension.bindToARA(document, knownRoles, assignedRoles);
        auto *model = ARA::PlugIn::fromRef<TuneLab::Cubase::ProbeDocumentController>(document);
        const DWORD hostThread = GetCurrentThreadId();
        hostEdits = std::make_shared<std::function<void()>>([this, model, hostThread] {
            if (GetCurrentThreadId() != hostThread)
            {
                OutputDebugStringA("TuneLab: ARA editor update requires the host main thread\n");
                return;
            }
            if (renderingActive && !model->isHostEditingDocument())
                rebuildPlayback(false);
        });
        model->observeHostEdits(hostEdits);
        modelPolls = std::make_shared<std::function<void()>>([this, model, hostThread] {
            if (GetCurrentThreadId() != hostThread || !renderingActive ||
                model->isHostEditingDocument() || completionNotified || !playback.ready())
                return;
            completionNotified = true;
            if (auto *renderer = extension.getPlaybackRenderer())
                for (auto *region : renderer->getPlaybackRegions())
                    model->notifyPlaybackRegionContentChanged(
                        region, ARA::ContentUpdateScopes::samplesAreAffected());
        });
        model->observeModelPolls(modelPolls);
        if (auto *view = extension.getEditorView<TuneLab::Cubase::BridgeEditorView>())
        {
            view->hostTime = hostTime;
            view->selectionChanged = [this](auto selected) {
                binding.select(selected);
                publishSelection();
            };
            view->setOpen(editorOpen);
        }
        else
            publishSelection();
        return result;
    }

    OBJ_METHODS(BridgeProbe, AudioEffect)
    DEFINE_INTERFACES
    DEF_INTERFACE(ARA::IPlugInEntryPoint)
    DEF_INTERFACE(ARA::IPlugInEntryPoint2)
    END_DEFINE_INTERFACES(AudioEffect)
    REFCOUNT_METHODS(AudioEffect)

  private:
    void publishSelection()
    {
        if (auto message = owned(allocateMessage()))
        {
            message->setMessageID("TuneLab.ARA.EditorBinding");
            message->getAttributes()->setInt("token", binding.id());
            sendMessage(message);
        }
    }
    bool renderingActive = false;
    bool completionNotified = false;
    std::shared_ptr<std::function<void()>> hostEdits;
    std::shared_ptr<std::function<void()>> modelPolls;
    TuneLab::Cubase::AraPlayback playback;
    std::shared_ptr<TuneLab::Cubase::HostTimeMailbox> hostTime =
        std::make_shared<TuneLab::Cubase::HostTimeMailbox>();
    bool editorOpen = false;
    TuneLab::Cubase::EditorBinding binding;
    ARA::PlugIn::PlugInExtension extension;
};

class MainFactory final : public ARA::IMainFactory
{
  public:
    MainFactory()
    {
        FUNKNOWN_CTOR
    }
    ~MainFactory(){FUNKNOWN_DTOR} DECLARE_FUNKNOWN_METHODS

        static FUnknown *create(void *)
    {
        return static_cast<ARA::IMainFactory *>(new MainFactory());
    }

    const ARA::ARAFactory *PLUGIN_API getFactory() override
    {
        return TuneLab::Cubase::getProbeFactory();
    }
};
IMPLEMENT_FUNKNOWN_METHODS(MainFactory, ARA::IMainFactory, ARA::IMainFactory::iid)

FUnknown *createController(void *)
{
    return static_cast<IEditController *>(new TuneLab::Cubase::EditorController());
}
} // namespace

BEGIN_FACTORY_DEF("TuneLab", "https://github.com/AI-Hobbyist/TuneLab", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(processorId), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "TuneLab ARA Bridge Probe", 0, "Fx|OnlyARA", "0.1.0", kVstVersionString,
           BridgeProbe::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(controllerId), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "TuneLab ARA Bridge Probe", 0, "", "0.1.0",
           kVstVersionString, createController)
DEF_CLASS2(INLINE_UID_FROM_FUID(factoryId), PClassInfo::kManyInstances, kARAMainFactoryClass,
           "TuneLab ARA Bridge Probe", 0, "", "0.1.0", kVstVersionString, MainFactory::create)
END_FACTORY
