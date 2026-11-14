#include "ARA_API/ARAVST3.h"
#include "ara_document.h"
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

    tresult PLUGIN_API process(ProcessData &data) override
    {
        // Discovery probe only. Region rendering is implemented in M3.
        for (int32 bus = 0; bus < data.numOutputs; ++bus)
        {
            auto &output = data.outputs[bus];
            for (int32 channel = 0; channel < output.numChannels; ++channel)
            {
                std::fill_n(output.channelBuffers32[channel], data.numSamples, 0.0f);
            }
            output.silenceFlags = (uint64(1) << output.numChannels) - 1;
        }
        return kResultOk;
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
        return extension.bindToARA(document, knownRoles, assignedRoles);
    }

    OBJ_METHODS(BridgeProbe, AudioEffect)
    DEFINE_INTERFACES
    DEF_INTERFACE(ARA::IPlugInEntryPoint)
    DEF_INTERFACE(ARA::IPlugInEntryPoint2)
    END_DEFINE_INTERFACES(AudioEffect)
    REFCOUNT_METHODS(AudioEffect)

  private:
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
    return static_cast<IEditController *>(new EditController());
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
