#pragma once

#include "base/source/timer.h"
#include "editor_binding.h"
#include "editor_view.h"
#include "instrument_parameters.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "project_state_stream.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <array>
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

class EditorController final : public Steinberg::Vst::EditController,
                               public Steinberg::ITimerCallback,
                               public Steinberg::Vst::INoteExpressionController
{
  public:
    explicit EditorController(bool instrument = false) : instrument(instrument)
    {
        attachmentCallback = std::make_shared<std::function<void()>>([this] { sendEditorOpen(); });
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
            lyricTimer = Steinberg::owned(Steinberg::Timer::create(this, 33));
            if (!lyricTimer)
                return Steinberg::kResultFalse;
        }
        return result;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override
    {
        if (instrument && Steinberg::FUnknownPrivate::iidEqual(
                              iid, Steinberg::Vst::INoteExpressionController::iid))
        {
            if (!object)
                return Steinberg::kInvalidArgument;
            *object = static_cast<Steinberg::Vst::INoteExpressionController *>(this);
            addRef();
            return Steinberg::kResultOk;
        }
        return EditController::queryInterface(iid, object);
    }
    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return EditController::addRef();
    }
    Steinberg::uint32 PLUGIN_API release() override
    {
        return EditController::release();
    }

    Steinberg::int32 PLUGIN_API getNoteExpressionCount(Steinberg::int32 bus,
                                                       Steinberg::int16 channel) override
    {
        return instrument && bus == 0 && channel >= 0 && channel < 16 ? 1 : 0;
    }
    Steinberg::tresult PLUGIN_API
    getNoteExpressionInfo(Steinberg::int32 bus, Steinberg::int16 channel, Steinberg::int32 index,
                          Steinberg::Vst::NoteExpressionTypeInfo &info) override
    {
        if (getNoteExpressionCount(bus, channel) != 1 || index != 0)
            return Steinberg::kResultFalse;
        info = {};
        info.typeId = Steinberg::Vst::kTextTypeID;
        std::copy_n(STR16("Lyrics"), 7, info.title);
        std::copy_n(STR16("Lyrics"), 7, info.shortTitle);
        info.unitId = -1;
        info.valueDesc.maximum = 1;
        info.flags = Steinberg::Vst::NoteExpressionTypeInfo::kIsAbsolute;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API getNoteExpressionStringByValue(
        Steinberg::int32, Steinberg::int16, Steinberg::Vst::NoteExpressionTypeID,
        Steinberg::Vst::NoteExpressionValue, Steinberg::Vst::String128) override
    {
        // Arbitrary text is carried by NoteExpressionTextEvent. A normalized
        // numeric value is not a lyric string and cannot encode one.
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API getNoteExpressionValueByString(
        Steinberg::int32, Steinberg::int16, Steinberg::Vst::NoteExpressionTypeID,
        const Steinberg::Vst::TChar *, Steinberg::Vst::NoteExpressionValue &) override
    {
        return Steinberg::kResultFalse;
    }
    ~EditorController() override
    {
        lyricTimer.reset();
        *attachmentCallback = {};
        if (document)
        {
            document->changed = nullptr;
            document->pollPosition = nullptr;
            document->pollStatus = nullptr;
        }
    }

    Steinberg::tresult PLUGIN_API terminate() override
    {
        lyricTimer.reset();
        return EditController::terminate();
    }

    void onTimer(Steinberg::Timer *) override
    {
        if (!getPeer())
            return;
        if (auto request = Steinberg::owned(allocateMessage()))
        {
            request->setMessageID("TuneLab.Lyrics.Request");
            sendMessage(request);
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
        if (result == Steinberg::kResultOk && !instrument)
            sendEditorOpen();
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
            if (instrument)
            {
                ensureDocument();
                if (getPeer())
                {
                    auto request = Steinberg::owned(allocateMessage());
                    if (!request)
                        return nullptr;
                    request->setMessageID("TuneLab.Pairing.Request");
                    if (sendMessage(request) != Steinberg::kResultOk)
                        return nullptr;
                }
            }
            auto selected =
                instrument ? (pairingStatus == 1 ? pairedDocument : document) : araDocument;
            auto *view = new EditorView(std::move(selected));
            if (!instrument)
                view->attachmentChanged = attachmentCallback;
            views.push_back(view->reference());
            return view;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return nullptr;
        }
    }

    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint *other) override
    {
        if (!instrument && other == getPeer())
        {
            for (const auto &entry : views)
                if (auto reference = entry.lock();
                    reference && reference->view &&
                    reference->view->setDocument(nullptr) != Steinberg::kResultOk)
                    return Steinberg::kResultFalse;
            araDocument.reset();
            araBound = false;
        }
        return EditController::disconnect(other);
    }

    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage *message) override
    {
        if (instrument && message && message->getMessageID() &&
            std::strcmp(message->getMessageID(), "TuneLab.Lyrics.Status") == 0)
            return receiveLyricStatus(message);
        if (instrument && message && message->getMessageID() &&
            std::strcmp(message->getMessageID(), "TuneLab.Lyrics.Event") == 0)
            return receiveNoteText(message);
        if (instrument && message && message->getMessageID() &&
            std::strcmp(message->getMessageID(), "TuneLab.Pairing.State") == 0)
            return selectPairedDocument(message);
        if (!instrument && message && message->getMessageID() &&
            std::strcmp(message->getMessageID(), "TuneLab.ARA.EditorBinding") == 0)
        {
            Steinberg::int64 token = 0;
            std::shared_ptr<EditorDocument> selected;
            if (message->getAttributes()->getInt("token", token) != Steinberg::kResultOk ||
                (token != 0 && !EditorBinding::resolve(token, selected)))
                return Steinberg::kInvalidArgument;
            for (auto entry = views.begin(); entry != views.end();)
            {
                if (auto reference = entry->lock(); reference && reference->view)
                {
                    if (reference->view->setDocument(selected) != Steinberg::kResultOk)
                        return Steinberg::kResultFalse;
                    ++entry;
                }
                else
                    entry = views.erase(entry);
            }
            const bool firstBinding = !araBound;
            araDocument = std::move(selected);
            araBound = true;
            if (firstBinding)
                sendEditorOpen();
            return Steinberg::kResultOk;
        }
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
    Steinberg::tresult receiveLyricStatus(Steinberg::Vst::IMessage *message)
    {
        Steinberg::int64 kind = 0;
        if (message->getAttributes()->getInt("kind", kind) != Steinberg::kResultOk || kind < 3 ||
            kind > 7)
            return Steinberg::kInvalidArgument;
        try
        {
            if (pairingStatus == 2)
                return Steinberg::kResultOk;
            ensureDocument();
            auto selected = pairingStatus == 1 ? pairedDocument : document;
            if (!selected || !selected->initialize())
                return Steinberg::kResultFalse;
            return selected->runtime.reportDocumentLyricStatus(selected->handle,
                                                               static_cast<std::int32_t>(kind)) == 0
                       ? Steinberg::kResultOk
                       : Steinberg::kResultFalse;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return Steinberg::kResultFalse;
        }
    }

    Steinberg::tresult receiveNoteText(Steinberg::Vst::IMessage *message)
    {
        double quarter = 0;
        Steinberg::int64 pitch = -1;
        const void *bytes = nullptr;
        Steinberg::uint32 length = 0;
        auto *attributes = message->getAttributes();
        if (attributes->getFloat("onsetQuarter", quarter) != Steinberg::kResultOk ||
            attributes->getInt("pitch", pitch) != Steinberg::kResultOk || pitch < 0 ||
            pitch > 127 || attributes->getBinary("text", bytes, length) != Steinberg::kResultOk ||
            length > 512 || length % sizeof(wchar_t) != 0 || (!bytes && length != 0))
            return Steinberg::kInvalidArgument;
        try
        {
            if (pairingStatus == 2)
                return Steinberg::kResultOk;
            ensureDocument();
            auto selected = pairingStatus == 1 ? pairedDocument : document;
            if (!selected || !selected->initialize())
                return Steinberg::kResultFalse;
            std::uint64_t sourceId = 0;
            std::int32_t disposition = 3;
            const auto result = selected->runtime.applyDocumentLyric(
                selected->handle, quarter, static_cast<std::int32_t>(pitch),
                static_cast<const wchar_t *>(bytes),
                static_cast<std::int32_t>(length / sizeof(wchar_t)), &sourceId, &disposition);
            if (result != 0)
            {
                selected->runtime.reportDocumentLyricStatus(selected->handle, result == 7 ? 5 : 6);
                return Steinberg::kResultFalse;
            }
            return Steinberg::kResultOk;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return Steinberg::kResultFalse;
        }
    }

    Steinberg::tresult selectPairedDocument(Steinberg::Vst::IMessage *message)
    {
        Steinberg::int64 status = -1;
        const void *endpoint = nullptr, *owner = nullptr;
        Steinberg::uint32 endpointSize = 0, ownerSize = 0;
        auto *attributes = message->getAttributes();
        if (attributes->getInt("status", status) != Steinberg::kResultOk || status < 0 ||
            status > 2 ||
            attributes->getBinary("endpoint", endpoint, endpointSize) != Steinberg::kResultOk ||
            !endpoint || endpointSize != 16 ||
            attributes->getBinary("owner", owner, ownerSize) != Steinberg::kResultOk || !owner ||
            ownerSize != 16)
            return Steinberg::kInvalidArgument;
        std::array<std::uint8_t, 16> nextEndpoint{}, nextOwner{};
        std::memcpy(nextEndpoint.data(), endpoint, 16);
        std::memcpy(nextOwner.data(), owner, 16);
        if (status == pairingStatus && nextEndpoint == pairedEndpoint && nextOwner == pairedOwner)
            return Steinberg::kResultOk;
        try
        {
            std::shared_ptr<EditorDocument> selected;
            if (status == 0 || status == 2)
            {
                ensureDocument();
                if (!document->setPairEndpoint(nextEndpoint))
                    return Steinberg::kResultFalse;
                selected = document;
            }
            else if (status == 1)
            {
                selected = std::make_shared<EditorDocument>();
                if (!selected->runtime.initialize(managedBundleDirectory()) ||
                    selected->runtime.createPairDocumentProxy(nextEndpoint.data(),
                                                              &selected->handle) != 0)
                    return Steinberg::kResultFalse;
            }
            // The session hides the independent score while its endpoint is
            // PeerMissing, leaving only explicit rebind/unlink controls.
            for (auto entry = views.begin(); entry != views.end();)
            {
                if (auto reference = entry->lock(); reference && reference->view)
                {
                    if (reference->view->setDocument(selected) != Steinberg::kResultOk)
                        return Steinberg::kResultFalse;
                    ++entry;
                }
                else
                    entry = views.erase(entry);
            }
            const bool editedPairing = status != 2 && (status != pairingStatus ||
                                                       (status == 1 && nextOwner != pairedOwner));
            pairedDocument = status == 1 ? std::move(selected) : nullptr;
            pairingStatus = static_cast<int>(status);
            pairedEndpoint = nextEndpoint;
            pairedOwner = nextOwner;
            if (editedPairing)
            {
                Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler2> handler(
                    getComponentHandler());
                if (handler)
                    handler->setDirty(true);
            }
            return Steinberg::kResultOk;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return Steinberg::kResultFalse;
        }
    }

    void sendEditorOpen()
    {
        bool open = false;
        for (const auto &entry : views)
            if (auto reference = entry.lock();
                reference && reference->view && reference->view->isAttached())
                open = true;
        if (auto message = Steinberg::owned(allocateMessage()))
        {
            message->setMessageID("TuneLab.ARA.EditorOpen");
            message->getAttributes()->setInt("open", open ? 1 : 0);
            sendMessage(message);
        }
    }
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
    std::shared_ptr<EditorDocument> araDocument;
    std::shared_ptr<EditorDocument> pairedDocument;
    std::array<std::uint8_t, 16> pairedEndpoint{}, pairedOwner{};
    int pairingStatus = 0;
    std::vector<std::weak_ptr<EditorView::Reference>> views;
    std::shared_ptr<std::function<void()>> attachmentCallback;
    bool araBound = false;
    bool instrument;
    Steinberg::IPtr<Steinberg::Timer> lyricTimer;
    std::atomic<double> musicalPosition{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<std::int32_t> songResult{-1}, previewResult{-1};
    std::atomic<std::int64_t> previewElapsed{0};
    std::atomic<bool> statusKnown{false};
};
} // namespace TuneLab::Cubase
