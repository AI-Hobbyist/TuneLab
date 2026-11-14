#pragma once

#include "editor_view.h"
#include "instrument_parameters.h"
#include "project_state_stream.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <atomic>

namespace TuneLab::Cubase
{
// This address belongs to the plug-in module, independent of the host executable.
inline std::filesystem::path managedBundleDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&managedBundleDirectory), &module))
    {
        throw std::runtime_error("Cannot locate the plug-in module");
    }
    std::vector<wchar_t> filename(32768);
    const DWORD length =
        GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
    if (length == 0 || length >= filename.size())
    {
        throw std::runtime_error("Cannot read the plug-in module path");
    }
    return std::filesystem::path(filename.data()).parent_path().parent_path() / L"Resources" /
           L"managed";
}

class EditorController final : public Steinberg::Vst::EditController
{
  public:
    explicit EditorController(bool instrument = false) : instrument(instrument)
    {
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) override
    {
        const auto result = EditController::initialize(context);
        if (result == Steinberg::kResultOk && instrument)
        {
            parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
                                    Steinberg::Vst::ParameterInfo::kCanAutomate |
                                        Steinberg::Vst::ParameterInfo::kIsBypass,
                                    bypassParameterId);
        }
        return result;
    }
    ~EditorController() override
    {
        if (document)
        {
            document->changed = nullptr;
            document->pollPosition = nullptr;
            document->pollStatus = nullptr;
        }
    }

    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream *stream) override
    {
        try
        {
            std::vector<std::uint8_t> state;
            const auto result = readProjectState(stream, state);
            if (result != Steinberg::kResultOk)
            {
                return result;
            }
            bool bypass = false;
            if (instrument && readInstrumentParameters(stream, bypass) != Steinberg::kResultOk)
                return Steinberg::kResultFalse;
            ensureDocument();
            if (document->handle != 0 &&
                document->runtime.restoreDocumentState(
                    document->handle, state.data(), static_cast<std::int32_t>(state.size())) != 0)
            {
                return Steinberg::kResultFalse;
            }
            document->state = std::move(state);
            if (instrument)
                setParamNormalized(bypassParameterId, bypass ? 1 : 0);
            return Steinberg::kResultOk;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return Steinberg::kResultFalse;
        }
    }

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint *other) override
    {
        const auto result = EditController::connect(other);
        if (result == Steinberg::kResultOk && document && !document->state.empty())
        {
            sendSnapshot(document->state);
        }
        return result;
    }
    Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) override
    {
        if (!name || std::strcmp(name, Steinberg::Vst::ViewType::kEditor) != 0)
        {
            return nullptr;
        }
        try
        {
            ensureDocument();
            return new EditorView(document);
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return nullptr;
        }
    }

    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage *message) override
    {
        if (message && message->getMessageID() &&
            std::strcmp(message->getMessageID(), "TuneLab.Transport.Position") == 0)
        {
            double position;
            const auto result = message->getAttributes()->getFloat("quarterNotes", position);
            if (result == Steinberg::kResultOk)
                musicalPosition.store(position);
            Steinberg::int64 song, preview, elapsed;
            auto *attributes = message->getAttributes();
            if (attributes->getInt("songResult", song) == Steinberg::kResultOk &&
                attributes->getInt("previewResult", preview) == Steinberg::kResultOk &&
                attributes->getInt("previewMicroseconds", elapsed) == Steinberg::kResultOk)
            {
                songResult.store(static_cast<std::int32_t>(song));
                previewResult.store(static_cast<std::int32_t>(preview));
                previewElapsed.store(elapsed);
                statusKnown.store(true);
            }
            return result;
        }
        return EditController::notify(message);
    }

  private:
    void ensureDocument()
    {
        if (!document)
        {
            document = std::make_shared<EditorDocument>();
            document->managedDirectory = managedBundleDirectory();
            document->pollPosition = [this] {
                if (auto request = Steinberg::owned(allocateMessage()))
                {
                    request->setMessageID("TuneLab.Transport.Request");
                    if (sendMessage(request) == Steinberg::kResultOk)
                        return musicalPosition.load();
                }
                statusKnown.store(false);
                return std::numeric_limits<double>::quiet_NaN();
            };
            document->pollStatus = [this](auto *song, auto *preview,
                                          auto *elapsed) -> std::int32_t {
                if (!statusKnown.load())
                {
                    return 2;
                }
                *song = songResult.load();
                *preview = previewResult.load();
                *elapsed = previewElapsed.load();
                return 0;
            };
            document->changed = [this](const auto &state) {
                sendSnapshot(state);
                Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler2> handler(
                    getComponentHandler());
                if (handler)
                {
                    handler->setDirty(true);
                }
            };
        }
    }

    void sendSnapshot(const std::vector<std::uint8_t> &state)
    {
        if (auto message = Steinberg::owned(allocateMessage()))
        {
            message->setMessageID("TuneLab.ProjectState");
            if (message->getAttributes()->setBinary("state", state.data(),
                                                    static_cast<Steinberg::uint32>(state.size())) ==
                    Steinberg::kResultOk &&
                sendMessage(message) == Steinberg::kResultOk)
            {
                return;
            }
        }
        OutputDebugStringA("TuneLab: processor rejected editor state\n");
    }

    std::shared_ptr<EditorDocument> document;
    bool instrument;
    std::atomic<double> musicalPosition{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<std::int32_t> songResult{-1}, previewResult{-1};
    std::atomic<std::int64_t> previewElapsed{0};
    std::atomic<bool> statusKnown{false};
};
} // namespace TuneLab::Cubase
