#include "editor_controller.h"
#include "host_time.h"
#include "instrument_parameters.h"
#include "midi_preview_player.h"
#include "note_text_ingress.h"
#include "pair_endpoint.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "probe_state.h"
#include "project_state_stream.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "render_worker.h"
#include <algorithm>
#include <cmath>
#include <optional>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
const FUID processorId(0xAC17B8A2, 0x74DA4B52, 0xB7E1C5A9, 0x5DB09621);
const FUID controllerId(0x769C2F63, 0xE9474138, 0xA2B6D450, 0x817AC39D);

class InstrumentProbe final : public AudioEffect
{
  public:
    InstrumentProbe()
    {
        setControllerClass(controllerId);
        processContextRequirements.needProjectTimeMusic()
            .needTransportState()
            .needTempo()
            .needTimeSignature()
            .needBarPositionMusic();
    }

    static FUnknown *create(void *)
    {
        return static_cast<IAudioProcessor *>(new InstrumentProbe());
    }

    tresult PLUGIN_API initialize(FUnknown *context) override
    {
        const auto result = AudioEffect::initialize(context);
        if (result != kResultOk)
        {
            return result;
        }
        addAudioOutput(STR16("Stereo Output"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI Input"), 16);
        controlThread = GetCurrentThreadId();
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream *stream) override
    {
        const auto result = TuneLab::Cubase::writeProjectState(stream, state);
        return result == kResultOk
                   ? TuneLab::Cubase::writeInstrumentParameters(stream, parameters.bypassed(),
                                                                !pairing.allowsProjectOutput())
                   : result;
    }

    tresult PLUGIN_API notify(IMessage *message) override
    {
        if (!message || !message->getMessageID())
        {
            return kInvalidArgument;
        }
        if (std::strcmp(message->getMessageID(), "TuneLab.Pairing.Request") == 0)
        {
            if (!enablePairingNotifications())
                return kResultFalse;
            return sendPairingState();
        }
        if (std::strcmp(message->getMessageID(), "TuneLab.Pairing.Bind") == 0)
        {
            const void *peer = nullptr;
            uint32 length = 0;
            if (message->getAttributes()->getBinary("bridge", peer, length) != kResultOk || !peer ||
                length != 16)
                return kInvalidArgument;
            if (!enablePairingNotifications() ||
                pairing.pairWith(static_cast<const std::uint8_t *>(peer)) != 0)
                return kResultFalse;
            pairing.publishPendingState();
            ++textRevision;
            return kResultOk;
        }
        if (std::strcmp(message->getMessageID(), "TuneLab.Pairing.Unlink") == 0)
        {
            if (!enablePairingNotifications() || pairing.unpair() != 0)
                return kResultFalse;
            pairing.publishPendingState();
            ++textRevision;
            return kResultOk;
        }
        if (std::strcmp(message->getMessageID(), "TuneLab.Lyrics.Request") == 0)
            return drainNoteText();
        if (std::strcmp(message->getMessageID(), "TuneLab.Transport.Request") == 0)
        {
            if (runtimeReady)
            {
                pairing.setStateChangedCallback(onPairStateChanged, this);
                pairing.publishPendingState();
            }
            TuneLab::Cubase::HostTimeSnapshot context;
            if (!hostTime.tryRead(context))
                return kResultFalse;
            auto response = owned(allocateMessage());
            if (!response)
                return kResultFalse;
            response->setMessageID("TuneLab.Transport.Position");
            response->getAttributes()->setFloat("quarterNotes", context.quarterNotes);
            response->getAttributes()->setFloat("sampleRate", context.sampleRate);
            response->getAttributes()->setInt("samplePosition", context.samplePosition);
            response->getAttributes()->setInt("timeFlags", context.flags);
            response->getAttributes()->setFloat("tempo", context.tempo);
            response->getAttributes()->setFloat("barQuarterNotes", context.barQuarterNotes);
            response->getAttributes()->setInt("numerator", context.numerator);
            response->getAttributes()->setInt("denominator", context.denominator);
            response->getAttributes()->setInt("songResult", worker ? worker->result() : -1);
            response->getAttributes()->setInt("previewResult", preview ? preview->result() : -1);
            response->getAttributes()->setInt("previewMicroseconds",
                                              preview ? preview->renderMicroseconds() : 0);
            return sendMessage(response);
        }
        if (std::strcmp(message->getMessageID(), "TuneLab.ProjectState") != 0)
        {
            return AudioEffect::notify(message);
        }
        const void *bytes = nullptr;
        uint32 length = 0;
        if (message->getAttributes()->getBinary("state", bytes, length) != kResultOk || !bytes ||
            length > static_cast<uint32>((std::numeric_limits<int32>::max)()))
        {
            return kInvalidArgument;
        }
        MemoryStream stream(const_cast<void *>(bytes), static_cast<TSize>(length));
        return restoreState(&stream, false);
    }

    tresult PLUGIN_API setState(IBStream *stream) override
    {
        return restoreState(stream, true);
    }

    tresult restoreState(IBStream *stream, bool restoreParameters)
    {
        try
        {
            std::vector<std::uint8_t> candidate;
            const auto result = TuneLab::Cubase::readProjectState(stream, candidate);
            if (result != kResultOk)
            {
                return result;
            }
            bool bypass = false, requiresPair = false;
            if (restoreParameters && TuneLab::Cubase::readInstrumentParameters(
                                         stream, bypass, requiresPair) != kResultOk)
                return kResultFalse;
            if (!candidate.empty() &&
                (!ensureRuntime() ||
                 runtime.validateProjectState(candidate.data(),
                                              static_cast<std::int32_t>(candidate.size())) != 0))
            {
                return kResultFalse;
            }
            if (!candidate.empty() && processSetup.sampleRate > 0 && !submitState(candidate))
            {
                return kResultFalse;
            }
            if (candidate.empty() && worker)
            {
                worker->invalidate();
            }
            if (candidate.empty() && preview)
            {
                preview->update({}, static_cast<int>(processSetup.sampleRate));
            }
            if (restoreParameters && (requiresPair || runtimeReady) &&
                (!ensureRuntime() || pairing.restoreIntent(requiresPair) != 0))
                return kResultFalse;
            state = std::move(candidate);
            // A host reload changes the ingress document. Local editor
            // commits retain its host baseline and must not erase queued
            // lyrics waiting for that edit to settle.
            if (restoreParameters)
                ++textRevision;
            if (restoreParameters)
                parameters.restore(bypass);
            return kResultOk;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return kResultFalse;
        }
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) override
    {
        try
        {
            const auto result = AudioEffect::setupProcessing(setup);
            return result == kResultOk && (state.empty() || submitState(state)) ? result
                                                                                : kResultFalse;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return kResultFalse;
        }
    }

    tresult PLUGIN_API setProcessing(TBool enabled) override
    {
        processingEnabled.store(enabled != 0);
        if (!enabled)
        {
            if (auto *player = publishedPreview.load())
            {
                player->reset();
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool active) override
    {
        if (active && !state.empty() && processSetup.processMode == kOffline &&
            (!worker || !worker->wait(renderGeneration, std::chrono::minutes(5)) ||
             worker->result() != 0))
        {
            return kResultFalse;
        }
        return AudioEffect::setActive(active);
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API terminate() override
    {
        publishedWorker.store(nullptr);
        publishedPreview.store(nullptr);
        worker.reset();
        preview.reset();
        pairing.reset();
        return AudioEffect::terminate();
    }

    tresult PLUGIN_API process(ProcessData &data) override
    {
        // Only publish a lock-free timing observation here. Messages and UI work run on
        // the editor's polling thread, never on the audio callback.
        hostTime.publish(data.processContext);
        noteText.capture(data.inputEvents, data.numSamples, data.processContext,
                         textRevision.load() + pairing.notificationRevision());
        const bool ownsProjectOutput = pairing.allowsProjectOutput();
        for (int32 bus = 0; bus < data.numOutputs; ++bus)
        {
            auto &output = data.outputs[bus];
            for (int32 channel = 0; channel < output.numChannels; ++channel)
            {
                if (data.symbolicSampleSize == kSample32)
                {
                    std::fill_n(output.channelBuffers32[channel], data.numSamples, 0.0f);
                }
                else
                {
                    std::fill_n(output.channelBuffers64[channel], data.numSamples, 0.0);
                }
            }
            output.silenceFlags = (uint64(1) << output.numChannels) - 1;
        }
        auto *renderer = publishedWorker.load();
        if (renderer && ownsProjectOutput && processingEnabled.load() && data.numOutputs > 0 &&
            data.outputs[0].numChannels >= 2 && data.numSamples > 0 && data.processContext &&
            (data.processContext->state & ProcessContext::kPlaying))
        {
            auto &output = data.outputs[0];
            bool audible;
            if (data.symbolicSampleSize == kSample32)
            {
                audible =
                    renderer->copyStereo(output.channelBuffers32[0], output.channelBuffers32[1],
                                         data.numSamples, data.processContext->projectTimeSamples,
                                         static_cast<std::int32_t>(processSetup.sampleRate));
            }
            else
            {
                audible =
                    renderer->copyStereo(output.channelBuffers64[0], output.channelBuffers64[1],
                                         data.numSamples, data.processContext->projectTimeSamples,
                                         static_cast<std::int32_t>(processSetup.sampleRate));
            }
            if (audible)
            {
                output.silenceFlags = 0;
            }
        }
        if (auto *player = publishedPreview.load(); player && processingEnabled.load())
        {
            if (!ownsProjectOutput)
            {
                // Paired playback belongs to ARA. Do not reinterpret incoming
                // track MIDI as an additional singing preview on this output.
                player->reset();
                player->process<float>(nullptr, nullptr, nullptr, data.numSamples,
                                       static_cast<int>(processSetup.sampleRate));
                parameters.process(data);
                return kResultOk;
            }
            if (data.numOutputs == 0 || data.outputs[0].numChannels < 2)
            {
                // Hosts may flush Note Off without any audio output buffers.
                player->process<float>(data.inputEvents, nullptr, nullptr, data.numSamples,
                                       static_cast<int>(processSetup.sampleRate));
                parameters.process(data);
                return kResultOk;
            }
            auto &output = data.outputs[0];
            const bool audible = data.symbolicSampleSize == kSample32
                                     ? player->process(data.inputEvents, output.channelBuffers32[0],
                                                       output.channelBuffers32[1], data.numSamples,
                                                       static_cast<int>(processSetup.sampleRate))
                                     : player->process(data.inputEvents, output.channelBuffers64[0],
                                                       output.channelBuffers64[1], data.numSamples,
                                                       static_cast<int>(processSetup.sampleRate));
            if (audible)
            {
                output.silenceFlags = 0;
            }
        }
        parameters.process(data);
        return kResultOk;
    }

  private:
    tresult sendLyricStatus(std::int32_t kind)
    {
        auto message = owned(allocateMessage());
        if (!message)
            return kResultFalse;
        message->setMessageID("TuneLab.Lyrics.Status");
        if (message->getAttributes()->setInt("kind", kind) != kResultOk)
            return kResultFalse;
        return sendMessage(message);
    }

    tresult drainNoteText()
    {
        if (GetCurrentThreadId() != controlThread || deliveringText)
            return kResultFalse;
        if (runtimeReady)
            pairing.publishPendingState();
        const auto lost = noteText.input.lostEvents();
        if (lost != lostTextEvents)
        {
            noteText.input.discardPending();
            pendingText.reset();
            if (sendLyricStatus(4) != kResultOk)
                return kResultFalse;
            lostTextEvents = lost;
            OutputDebugStringA("TuneLab: lyric ingress overflow; incomplete batch discarded\n");
        }
        const auto rejected = noteText.input.rejectedEvents();
        if (rejected != rejectedTextEvents)
        {
            if (sendLyricStatus(7) != kResultOk)
                return kResultFalse;
            rejectedTextEvents = rejected;
        }
        for (int count = 0; count < 64; ++count)
        {
            if (!pendingText)
            {
                TuneLab::Cubase::NoteTextIngress::Record record;
                if (!noteText.input.pop(record))
                    break;
                pendingText = record;
            }
            const auto &record = *pendingText;
            if (record.revision != textRevision.load() + pairing.notificationRevision())
            {
                pendingText.reset();
                continue;
            }
            if (record.pitch < 0)
            {
                if (sendLyricStatus(3) != kResultOk)
                    return kResultFalse;
                pendingText.reset();
                continue;
            }
            auto message = owned(allocateMessage());
            if (!message)
                return kResultFalse;
            message->setMessageID("TuneLab.Lyrics.Event");
            auto *attributes = message->getAttributes();
            if (attributes->setFloat("onsetQuarter", record.onsetQuarter) != kResultOk ||
                attributes->setInt("pitch", record.pitch) != kResultOk ||
                attributes->setBinary("text", record.text.data(), record.length * sizeof(TChar)) !=
                    kResultOk)
                return kResultFalse;
            // A synchronous editor callback must not recursively drain the
            // same pending record before the outer delivery completes.
            deliveringText = true;
            const auto result = sendMessage(message);
            deliveringText = false;
            if (result != kResultOk)
                return result;
            pendingText.reset();
        }
        return kResultOk;
    }

    tresult sendPairingState()
    {
        if (!ensureRuntime())
            return kResultFalse;
        auto response = owned(allocateMessage());
        std::array<std::uint8_t, 16> owner{};
        std::int32_t status = 0, output = 0;
        if (!response || pairing.state(owner, status, output) != 0)
            return kResultFalse;
        if (textPairStatus >= 0 && (textPairStatus != status || textPairOwner != owner))
            ++textRevision;
        textPairStatus = status;
        textPairOwner = owner;
        response->setMessageID("TuneLab.Pairing.State");
        response->getAttributes()->setBinary("endpoint", pairing.id().data(), 16);
        response->getAttributes()->setBinary("owner", owner.data(), 16);
        response->getAttributes()->setInt("status", status);
        response->getAttributes()->setInt("projectOutput", output);
        return sendMessage(response);
    }

    static bool onPairStateChanged(void *owner) noexcept
    {
        try
        {
            auto &instrument = *static_cast<InstrumentProbe *>(owner);
            return instrument.getPeer() && instrument.sendPairingState() == kResultOk;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return false;
        }
    }

    bool enablePairingNotifications()
    {
        if (!ensureRuntime())
            return false;
        pairing.setStateChangedCallback(onPairStateChanged, this);
        return true;
    }

    bool ensureRuntime()
    {
        if (!runtimeReady)
        {
            runtimeReady = runtime.initialize(TuneLab::Cubase::managedBundleDirectory());
        }
        return runtimeReady && pairing.initialize(runtime, 1);
    }

    bool submitState(const std::vector<std::uint8_t> &snapshot)
    {
        if (!worker)
        {
            if (!ensureRuntime())
            {
                return false;
            }
            worker = std::make_unique<TuneLab::Cubase::RenderWorker>(runtime);
            publishedWorker.store(worker.get());
        }
        renderGeneration =
            worker->submit(snapshot, static_cast<std::int32_t>(processSetup.sampleRate));
        if (!preview)
        {
            preview = std::make_unique<TuneLab::Cubase::MidiPreviewPlayer>(runtime);
            publishedPreview.store(preview.get());
        }
        preview->update(snapshot, static_cast<int>(processSetup.sampleRate));
        return true;
    }

    std::vector<std::uint8_t> state;
    TuneLab::Cubase::ManagedRuntime runtime;
    TuneLab::Cubase::PairEndpoint pairing;
    bool runtimeReady = false;
    std::unique_ptr<TuneLab::Cubase::RenderWorker> worker;
    std::atomic<TuneLab::Cubase::RenderWorker *> publishedWorker{nullptr};
    std::unique_ptr<TuneLab::Cubase::MidiPreviewPlayer> preview;
    std::atomic<TuneLab::Cubase::MidiPreviewPlayer *> publishedPreview{nullptr};
    std::uint64_t renderGeneration = 0;
    std::atomic<bool> processingEnabled{false};
    TuneLab::Cubase::HostTimeMailbox hostTime;
    TuneLab::Cubase::InstrumentParameters parameters;
    TuneLab::Cubase::NoteTextIngress noteText;
    std::optional<TuneLab::Cubase::NoteTextIngress::Record> pendingText;
    std::atomic<std::uint64_t> textRevision{1};
    std::uint64_t lostTextEvents = 0;
    std::uint64_t rejectedTextEvents = 0;
    DWORD controlThread = 0;
    bool deliveringText = false;
    std::array<std::uint8_t, 16> textPairOwner{};
    std::int32_t textPairStatus = -1;
};

FUnknown *createController(void *)
{
    return static_cast<IEditController *>(new TuneLab::Cubase::EditorController(true));
}
} // namespace

BEGIN_FACTORY_DEF("TuneLab", "https://github.com/AI-Hobbyist/TuneLab", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(processorId), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "TuneLab Instrument Probe", 0, "Instrument|Synth", "0.1.0", kVstVersionString,
           InstrumentProbe::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(controllerId), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "TuneLab Instrument Probe Controller", 0, "", "0.1.0",
           kVstVersionString, createController)
END_FACTORY
