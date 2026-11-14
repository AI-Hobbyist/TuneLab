#include "probe_state.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <algorithm>

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
        addEventInput(STR16("MIDI Input"), 1);
        return kResultOk;
    }

    // M0 validates discovery and lifecycle only; synthesis arrives in M2.
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        return TuneLab::Cubase::writeProbeState(stream);
    }

    tresult PLUGIN_API setState(IBStream *stream) override
    {
        return TuneLab::Cubase::readProbeState(stream);
    }

    tresult PLUGIN_API process(ProcessData &data) override
    {
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
        return kResultOk;
    }
};

FUnknown *createController(void *)
{
    return static_cast<IEditController *>(new EditController());
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
