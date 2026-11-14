#pragma once

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <Windows.h>
#include <filesystem>

namespace TuneLab::Cubase
{
// Command-line integration fixture. Loads the actual Instrument module while
// the ARA probe owns the actual Bridge factory and SDK document controller.
struct PairedInstrumentFixture
{
    HMODULE module = nullptr;
    bool(__cdecl *exitModule)() = nullptr;
    Steinberg::IPtr<Steinberg::IPluginFactory> factory;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> componentConnection,
        controllerConnection;
    bool componentInitialized = false, controllerInitialized = false;

    bool initialize(Steinberg::FUnknown *host)
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, 32768))
            return false;
        const auto binary = std::filesystem::path(executable).parent_path().parent_path() /
                            L"VST3/Debug/TuneLabInstrumentProbe.vst3/Contents/x86_64-win/"
                            L"TuneLabInstrumentProbe.vst3";
        module = LoadLibraryW(binary.c_str());
        if (!module)
            return false;
        const auto initializeModule =
            reinterpret_cast<bool(__cdecl *)()>(GetProcAddress(module, "InitDll"));
        const auto getFactory = reinterpret_cast<IPluginFactory *(PLUGIN_API *)()>(
            GetProcAddress(module, "GetPluginFactory"));
        if (!initializeModule || !getFactory || !initializeModule())
            return false;
        exitModule = reinterpret_cast<bool(__cdecl *)()>(GetProcAddress(module, "ExitDll"));
        factory = owned(getFactory());
        IComponent *rawComponent = nullptr;
        IEditController *rawController = nullptr;
        const FUID componentId(0xAC17B8A2, 0x74DA4B52, 0xB7E1C5A9, 0x5DB09621);
        const FUID controllerId(0x769C2F63, 0xE9474138, 0xA2B6D450, 0x817AC39D);
        if (!factory ||
            factory->createInstance(componentId, IComponent::iid,
                                    reinterpret_cast<void **>(&rawComponent)) != kResultOk)
            return false;
        component = owned(rawComponent);
        if (factory->createInstance(controllerId, IEditController::iid,
                                    reinterpret_cast<void **>(&rawController)) != kResultOk)
            return false;
        controller = owned(rawController);
        if (component->initialize(host) != kResultOk)
            return false;
        componentInitialized = true;
        if (controller->initialize(host) != kResultOk)
            return false;
        controllerInitialized = true;
        processor = FUnknownPtr<IAudioProcessor>(component);
        componentConnection = FUnknownPtr<IConnectionPoint>(component);
        controllerConnection = FUnknownPtr<IConnectionPoint>(controller);
        return processor && componentConnection && controllerConnection &&
               componentConnection->connect(controllerConnection) == kResultOk &&
               controllerConnection->connect(componentConnection) == kResultOk;
    }

    ~PairedInstrumentFixture()
    {
        if (componentConnection && controllerConnection)
        {
            componentConnection->disconnect(controllerConnection);
            controllerConnection->disconnect(componentConnection);
        }
        componentConnection = nullptr;
        controllerConnection = nullptr;
        if (componentInitialized)
        {
            if (processor)
                processor->setProcessing(false);
            component->setActive(false);
            component->terminate();
        }
        if (controllerInitialized)
            controller->terminate();
        processor = nullptr;
        controller = nullptr;
        component = nullptr;
        factory = nullptr;
        if (exitModule)
            exitModule();
        if (module)
            FreeLibrary(module);
    }
};
} // namespace TuneLab::Cubase
