#pragma once

#include "host_time.h"

#include "ARA_Library/PlugIn/ARAPlug.h"
#include "ara_archive.h"
#include "ara_archive_bundle.h"
#include "ara_key_preview.h"
#include "ara_note_identity.h"
#include "ara_timeline.h"
#include "editor_controller.h"
#include "pair_endpoint.h"
#include "pair_view_owner.h"
#include <array>
#include <optional>

namespace TuneLab::Cubase
{

class BridgeMusicalContext final : public ARA::PlugIn::MusicalContext
{
  public:
    using ARA::PlugIn::MusicalContext::MusicalContext;
    std::optional<AraTimeline> timeline;
    bool invalidTimeline = false;

    void refreshTimeline() noexcept
    {
        timeline.reset();
        invalidTimeline = false;
        try
        {
            ARA::PlugIn::HostContentReader<ARA::kARAContentTypeTempoEntries> reader(this);
            if (!reader)
                return;
            std::vector<ARA::ARAContentTempoEntry> points(reader.begin(), reader.end());
            ARA::PlugIn::HostContentReader<ARA::kARAContentTypeBarSignatures> meterReader(this);
            std::vector<ARA::ARAContentBarSignature> signatures(meterReader.begin(),
                                                                meterReader.end());
            timeline.emplace(std::move(points), std::move(signatures));
        }
        catch (const std::exception &exception)
        {
            invalidTimeline = true;
            OutputDebugStringA(exception.what());
        }
    }
};

class BridgeAudioSource final : public ARA::PlugIn::AudioSource
{
  public:
    using ARA::PlugIn::AudioSource::AudioSource;
    std::vector<ARA::ARAContentNote> notes;
    std::vector<std::uint64_t> noteIds;
    std::uint64_t nextNoteId = 0;
    std::uint64_t contentRevision = 0;
    bool notesAvailable = false;
    bool invalidNotes = false;

    void refreshNotes() noexcept
    {
        ++contentRevision;
        auto previous = std::move(notes);
        auto previousIds = std::move(noteIds);
        notes.clear();
        noteIds.clear();
        notesAvailable = false;
        invalidNotes = false;
        try
        {
            ARA::PlugIn::HostContentReader<ARA::kARAContentTypeNotes> reader(this);
            if (!reader)
                return;
            std::vector<ARA::ARAContentNote> snapshot(reader.begin(), reader.end());
            double previousStart = -std::numeric_limits<double>::infinity();
            for (const auto &note : snapshot)
            {
                if (!std::isfinite(note.startPosition) || note.startPosition < previousStart ||
                    !std::isfinite(note.noteDuration) || note.noteDuration < 0 ||
                    !std::isfinite(note.attackDuration) || !std::isfinite(note.signalDuration) ||
                    note.signalDuration < 0 || !std::isfinite(note.frequency) ||
                    note.frequency < 0 || !std::isfinite(note.volume) || note.volume < 0 ||
                    note.volume > 1)
                    throw std::invalid_argument("ARA source contains invalid note content");
                previousStart = note.startPosition;
            }
            auto ids = matchAraNoteIds(previous, previousIds, snapshot, nextNoteId);
            notes = std::move(snapshot);
            noteIds = std::move(ids);
            notesAvailable = true;
        }
        catch (const std::exception &exception)
        {
            invalidNotes = true;
            OutputDebugStringA(exception.what());
        }
    }
};

class BridgeAudioModification final : public ARA::PlugIn::AudioModification
{
  public:
    BridgeAudioModification(ARA::PlugIn::AudioSource *source, ARA::ARAAudioModificationHostRef host,
                            const ARA::PlugIn::AudioModification *clone)
        : AudioModification(source, host, clone), editor(std::make_shared<EditorDocument>())
    {
        editor->managedDirectory = managedBundleDirectory();
        if (clone)
            editor->state = static_cast<const BridgeAudioModification *>(clone)->editor->state;
        const DWORD hostThread = GetCurrentThreadId();
        editNotification = std::make_shared<std::function<void()>>([this, hostThread] {
            if (GetCurrentThreadId() != hostThread)
            {
                OutputDebugStringA(
                    "TuneLab: ARA content notification requires the host main thread\n");
                return;
            }
            // The SDK coalesces this until the host polls notifyModelUpdates.
            // Restores and host imports do not emit EditorDocument callbacks.
            getDocumentController()->notifyAudioModificationContentChanged(
                this, ARA::ContentUpdateScopes::everythingIsAffected());
        });
        editor->observeStateChanges(editNotification);
    }

    ~BridgeAudioModification() override
    {
        editor->keyPreview = {};
        editor->pollPosition = {};
    }

    PairEndpoint &pairEndpoint()
    {
        if (!editor->runtime.registerPairEndpoint &&
            !editor->runtime.initialize(editor->managedDirectory))
            throw std::runtime_error("ARA pairing runtime initialization failed");
        if (!pairing.initialize(editor->runtime, 2))
            throw std::runtime_error("ARA pairing endpoint registration failed");
        if (!documentAccessRegistered)
        {
            if (pairing.setDocumentAccess(copyPairDocument, applyPairDocument, this) != 0)
                throw std::runtime_error("ARA shared document registration failed");
            documentAccessRegistered = true;
        }
        if (!viewProviderRegistered)
        {
            if (PairViewOwner::registerWith(pairing, editor) != 0)
                throw std::runtime_error("ARA shared view registration failed");
            viewProviderRegistered = true;
        }
        if (!editor->setPairEndpoint(pairing.id()))
            throw std::runtime_error("ARA document pairing configuration failed");
        return pairing;
    }

    const std::shared_ptr<EditorDocument> &editorDocument() const noexcept
    {
        return editor;
    }

  private:
    static std::int32_t __cdecl copyPairDocument(void *owner, std::uint8_t *destination,
                                                 std::int32_t capacity,
                                                 std::int32_t *required) noexcept
    {
        auto &modification = *static_cast<BridgeAudioModification *>(owner);
        if (GetCurrentThreadId() != modification.hostThread)
            return static_cast<std::int32_t>(BridgeResult::WrongThread);
        const auto &state = modification.editor->state;
        if (!required || capacity < 0 || (!destination && capacity != 0))
            return static_cast<std::int32_t>(BridgeResult::InvalidArgument);
        *required = static_cast<std::int32_t>(state.size());
        if (state.empty())
            return static_cast<std::int32_t>(BridgeResult::InvalidHandle);
        if (!destination)
            return 0;
        if (capacity < *required)
            return static_cast<std::int32_t>(BridgeResult::InvalidArgument);
        std::copy(state.begin(), state.end(), destination);
        return 0;
    }
    static std::int32_t __cdecl applyPairDocument(void *owner, const std::uint8_t *bytes,
                                                  std::int32_t length) noexcept
    {
        try
        {
            auto &modification = *static_cast<BridgeAudioModification *>(owner);
            if (GetCurrentThreadId() != modification.hostThread)
                return static_cast<std::int32_t>(BridgeResult::WrongThread);
            auto &document = *modification.editor;
            if (!bytes || length <= 0 || document.runtime.validateProjectState(bytes, length) != 0)
                return static_cast<std::int32_t>(BridgeResult::InvalidArgument);
            if (document.handle != 0)
            {
                const auto result =
                    document.runtime.restoreAraDocumentState(document.handle, bytes, length);
                if (result != 0)
                    return result;
            }
            EditorDocument::onStateChanged(&document, bytes, length);
            return 0;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return static_cast<std::int32_t>(BridgeResult::Failed);
        }
    }
    const DWORD hostThread = GetCurrentThreadId();
    bool documentAccessRegistered = false;
    bool viewProviderRegistered = false;
    std::shared_ptr<EditorDocument> editor;
    PairEndpoint pairing;
    std::shared_ptr<std::function<void()>> editNotification;
};

class BridgePlaybackRegion final : public ARA::PlugIn::PlaybackRegion
{
  public:
    using ARA::PlugIn::PlaybackRegion::PlaybackRegion;
    struct Reference
    {
        BridgePlaybackRegion *region;
    };
    ~BridgePlaybackRegion() override
    {
        lifetime->region = nullptr;
    }

    std::weak_ptr<Reference> reference() const noexcept
    {
        return lifetime;
    }

    // Control thread only: project this region without replacing the shared
    // modification document or disturbing a pending editor update.
    std::optional<std::vector<std::uint8_t>> projectState() const
    {
        auto *modification = getAudioModification<BridgeAudioModification>();
        auto editor = modification->editorDocument();
        if (!editor->state.empty() && (editor->state.size() < 16 || editor->state[8] != 2))
            return editor->state;
        const auto *source = modification->getAudioSource<BridgeAudioSource>();
        const auto *context = getRegionSequence()->getMusicalContext<BridgeMusicalContext>();
        if (!source->notesAvailable || !context || !context->timeline ||
            getDurationInAudioModificationTime() <= 0 || getDurationInPlaybackTime() <= 0)
            return std::nullopt;
        if (!editor->runtime.buildAraProjectState &&
            !editor->runtime.initialize(editor->managedDirectory))
            return std::nullopt;
        std::vector<AraTempoInput> tempo;
        for (const auto &point : context->timeline->syncPoints())
            tempo.push_back({point.timePosition, point.quarterPosition});
        std::vector<AraMeterInput> meters;
        for (const auto &signature : context->timeline->barSignatures())
            meters.push_back({signature.position, signature.numerator, signature.denominator});
        std::vector<AraNoteInput> notes;
        for (std::size_t index = 0; index < source->notes.size(); ++index)
        {
            const auto &note = source->notes[index];
            notes.push_back(
                {source->noteIds[index], note.startPosition, note.noteDuration, note.pitchNumber});
        }
        const AraRegionInput region{getStartInAudioModificationTime(),
                                    getDurationInAudioModificationTime(), getStartInPlaybackTime(),
                                    getDurationInPlaybackTime()};
        auto currentState = editor->state;
        if (editor->handle != 0 && !editor->state.empty())
        {
            std::int32_t size = 0;
            if (editor->runtime.copyDocumentState(editor->handle, nullptr, 0, &size) != 0)
                return std::nullopt;
            currentState.resize(size);
            if (editor->runtime.copyDocumentState(editor->handle, currentState.data(), size,
                                                  &size) != 0)
                return std::nullopt;
        }
        std::int32_t required = 0;
        if (editor->runtime.buildAraProjectState(
                currentState.data(), static_cast<std::int32_t>(currentState.size()), tempo.data(),
                static_cast<std::int32_t>(tempo.size()), meters.data(),
                static_cast<std::int32_t>(meters.size()), notes.data(),
                static_cast<std::int32_t>(notes.size()), &region, nullptr, 0, &required) != 0)
            return std::nullopt;
        std::vector<std::uint8_t> state(required);
        if (editor->runtime.buildAraProjectState(
                currentState.data(), static_cast<std::int32_t>(currentState.size()), tempo.data(),
                static_cast<std::int32_t>(tempo.size()), meters.data(),
                static_cast<std::int32_t>(meters.size()), notes.data(),
                static_cast<std::int32_t>(notes.size()), &region, state.data(), required,
                &required) != 0)
            return std::nullopt;
        return state;
    }

    bool synchronizeScore() const
    {
        auto editor = getAudioModification<BridgeAudioModification>()->editorDocument();
        editor->retryPendingHostUpdate = {};
        auto projected = projectState();
        if (!projected)
            return false;
        auto state = std::move(*projected);
        const auto required = static_cast<std::int32_t>(state.size());
        if (state == editor->state)
            return true;
        if (editor->handle != 0)
        {
            const auto result =
                editor->runtime.restoreAraDocumentState(editor->handle, state.data(), required);
            if (result == static_cast<std::int32_t>(BridgeResult::Busy))
            {
                std::weak_ptr<Reference> regionLifetime = lifetime;
                std::weak_ptr<EditorDocument> owner = editor;
                editor->retryPendingHostUpdate = [regionLifetime, owner] {
                    if (auto reference = regionLifetime.lock(); reference && reference->region)
                        reference->region->synchronizeScore();
                    else if (auto document = owner.lock())
                        document->retryPendingHostUpdate = {};
                };
                return false;
            }
            if (result != 0)
                return false;
        }
        editor->state = std::move(state);
        return true;
    }

    // Audio lookup uses a half-open playback range. Empty host regions are
    // valid objects but contain no samples and have no inverse time mapping.
    std::optional<double> modificationSecondsAtSongSeconds(double seconds) const
    {
        if (!std::isfinite(seconds) || getDurationInPlaybackTime() <= 0 ||
            getDurationInAudioModificationTime() <= 0 || seconds < getStartInPlaybackTime() ||
            seconds >= getEndInPlaybackTime())
            return {};
        return getStartInAudioModificationTime() + (seconds - getStartInPlaybackTime()) *
                                                       getDurationInAudioModificationTime() /
                                                       getDurationInPlaybackTime();
    }

    std::optional<double> songQuarterAtModificationSeconds(double seconds) const
    {
        if (!std::isfinite(seconds) || getDurationInAudioModificationTime() <= 0 ||
            getDurationInPlaybackTime() <= 0 || seconds < getStartInAudioModificationTime() ||
            seconds > getEndInAudioModificationTime())
            return {};
        const auto *context = getRegionSequence()->getMusicalContext<BridgeMusicalContext>();
        if (!context || !context->timeline)
            return {};
        const double songSeconds =
            getStartInPlaybackTime() + (seconds - getStartInAudioModificationTime()) *
                                           getDurationInPlaybackTime() /
                                           getDurationInAudioModificationTime();
        return context->timeline->quarterAtSeconds(songSeconds);
    }

  private:
    std::shared_ptr<Reference> lifetime = std::make_shared<Reference>(Reference{this});
};

class BridgeEditorRenderer final : public ARA::PlugIn::EditorRenderer
{
  public:
    explicit BridgeEditorRenderer(ARA::PlugIn::DocumentController *controller)
        : EditorRenderer(controller)
    {
    }
    AraKeyPreview preview;
    void setActive(bool active, int maximumBlock, int rate)
    {
        enabled = active;
        if (active)
            preview.prepare(maximumBlock, rate);
        else
            preview.clear();
    }
    void request(const std::shared_ptr<EditorDocument> &document, int pitch, bool pressed)
    {
        if (!enabled)
            return;
        if (!pressed)
        {
            if (current.lock() == document)
                preview.stop();
            return;
        }
        BridgePlaybackRegion *match = nullptr;
        auto consider = [&](auto *region) {
            auto *candidate = static_cast<BridgePlaybackRegion *>(region);
            if (!match &&
                candidate->getAudioModification<BridgeAudioModification>()->editorDocument() ==
                    document)
                match = candidate;
        };
        for (auto *region : getPlaybackRegions())
            consider(region);
        for (auto *sequence : getRegionSequences())
            for (auto *region : sequence->getPlaybackRegions())
                consider(region);
        if (!match)
            return;
        auto state = match->projectState();
        if (!state)
            return;
        if (!document->runtime.renderPreview &&
            !document->runtime.initialize(document->managedDirectory))
            throw std::runtime_error("ARA preview runtime initialization failed");
        if (current.lock() != document || !changed)
        {
            std::weak_ptr<EditorDocument> owner = document;
            changed = std::make_shared<std::function<void()>>([this, owner] {
                if (current.lock() == owner.lock())
                    preview.stop();
            });
        }
        document->observeStateChanges(changed);
        current = document;
        preview.start(document->runtime, std::move(*state), pitch);
    }

  protected:
    void didRemovePlaybackRegion(ARA::PlugIn::PlaybackRegion *) noexcept override
    {
        preview.stop();
    }
    void didRemoveRegionSequence(ARA::PlugIn::RegionSequence *) noexcept override
    {
        preview.stop();
    }

  private:
    bool enabled = false;
    std::weak_ptr<EditorDocument> current;
    std::shared_ptr<std::function<void()>> changed;
};

// Resolve on demand: the SDK clears selections when a referenced region is
// destroyed. Never retain a raw region pointer across host editing cycles.
class BridgeEditorView final : public ARA::PlugIn::EditorView
{
  public:
    using ARA::PlugIn::EditorView::EditorView;
    std::function<void(std::shared_ptr<EditorDocument>)> selectionChanged;
    std::weak_ptr<HostTimeMailbox> hostTime;

    void setOpen(bool value)
    {
        open = value;
        setEditorOpen(value);
        refreshSelection();
    }
    void refreshSelection() noexcept
    {
        try
        {
            if (selectionChanged)
                selectionChanged(open ? selectedDocument() : nullptr);
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
        }
    }

    std::shared_ptr<EditorDocument> selectedDocument() const
    {
        const auto regions = getViewSelection().getEffectivePlaybackRegions();
        if (regions.empty())
            return {};
        auto *region = static_cast<BridgePlaybackRegion *>(regions.front());
        if (!region->synchronizeScore())
            OutputDebugStringA("TuneLab: ARA source synchronization is unavailable or deferred\n");
        auto *modification = region->getAudioModification<BridgeAudioModification>();
        modification->pairEndpoint();
        auto document = modification->editorDocument();
        auto clock = hostTime;
        auto lifetime = region->reference();
        document->sourcePosition = true;
        document->pollPosition = [clock, lifetime] {
            const double unavailable = std::numeric_limits<double>::quiet_NaN();
            auto mailbox = clock.lock();
            auto reference = lifetime.lock();
            HostTimeSnapshot position;
            if (!mailbox || !reference || !reference->region || !mailbox->tryRead(position) ||
                position.sampleRate <= 0)
                return unavailable;
            auto &region = *reference->region;
            if (region.getDurationInPlaybackTime() <= 0 ||
                region.getDurationInAudioModificationTime() <= 0)
                return unavailable;
            const double songSeconds =
                static_cast<double>(position.samplePosition) / position.sampleRate;
            return region.getStartInAudioModificationTime() +
                   (songSeconds - region.getStartInPlaybackTime()) *
                       region.getDurationInAudioModificationTime() /
                       region.getDurationInPlaybackTime();
        };
        return document;
    }

  protected:
    void doNotifySelection(const ARA::PlugIn::ViewSelection *) noexcept override
    {
        refreshSelection();
    }

  private:
    bool open = false;
};

// ARA owns modification identities and lifetimes. Each modification owns an
// independent TuneLab editing document; cloning copies bytes, never history.
class ProbeDocumentController final : public ARA::PlugIn::DocumentController
{
  public:
    using DocumentController::DocumentController;

    void observeHostEdits(const std::shared_ptr<std::function<void()>> &observer)
    {
        hostEditObservers.erase(std::remove_if(hostEditObservers.begin(), hostEditObservers.end(),
                                               [](const auto &entry) { return entry.expired(); }),
                                hostEditObservers.end());
        hostEditObservers.push_back(observer);
    }

    void observeModelPolls(const std::shared_ptr<std::function<void()>> &observer)
    {
        modelPollObservers.erase(std::remove_if(modelPollObservers.begin(),
                                                modelPollObservers.end(),
                                                [](const auto &entry) { return entry.expired(); }),
                                 modelPollObservers.end());
        modelPollObservers.push_back(observer);
    }

  protected:
    void willNotifyModelUpdates() noexcept override
    {
        for (const auto &entry : modelPollObservers)
            if (auto observer = entry.lock())
                (*observer)();
    }

    ARA::PlugIn::AudioSource *doCreateAudioSource(ARA::PlugIn::Document *document,
                                                  ARA::ARAAudioSourceHostRef host) noexcept override
    {
        return new BridgeAudioSource(document, host);
    }
    void didAddAudioSourceToDocument(ARA::PlugIn::Document *,
                                     ARA::PlugIn::AudioSource *source) noexcept override
    {
        static_cast<BridgeAudioSource *>(source)->refreshNotes();
    }

    ARA::PlugIn::MusicalContext *doCreateMusicalContext(
        ARA::PlugIn::Document *document, ARA::ARAMusicalContextHostRef host) noexcept override
    {
        return new BridgeMusicalContext(document, host);
    }
    void didAddMusicalContextToDocument(ARA::PlugIn::Document *,
                                        ARA::PlugIn::MusicalContext *context) noexcept override
    {
        static_cast<BridgeMusicalContext *>(context)->refreshTimeline();
    }

    void didEndEditing() noexcept override
    {
        for (auto *view : getEditorViews<BridgeEditorView>())
            view->refreshSelection();
        for (const auto &entry : hostEditObservers)
            if (auto observer = entry.lock())
                (*observer)();
    }

    ARA::PlugIn::EditorRenderer *doCreateEditorRenderer() noexcept override
    {
        return new BridgeEditorRenderer(this);
    }

    ARA::PlugIn::EditorView *doCreateEditorView() noexcept override
    {
        return new BridgeEditorView(this);
    }

    ARA::PlugIn::AudioModification *doCreateAudioModification(
        ARA::PlugIn::AudioSource *source, ARA::ARAAudioModificationHostRef host,
        const ARA::PlugIn::AudioModification *clone) noexcept override
    {
        auto *modification = new BridgeAudioModification(source, host, clone);
        std::weak_ptr<EditorDocument> document = modification->editorDocument();
        const DWORD hostThread = GetCurrentThreadId();
        modification->editorDocument()->keyPreview = [this, document, hostThread](int pitch,
                                                                                  bool pressed) {
            if (GetCurrentThreadId() != hostThread)
            {
                OutputDebugStringA("TuneLab: ARA preview requires the host main thread\n");
                return;
            }
            if (auto owner = document.lock())
                for (auto *renderer : getEditorRenderers<BridgeEditorRenderer>())
                    renderer->request(owner, pitch, pressed);
        };
        return modification;
    }
    ARA::PlugIn::PlaybackRegion *doCreatePlaybackRegion(
        ARA::PlugIn::AudioModification *modification,
        ARA::ARAPlaybackRegionHostRef host) noexcept override
    {
        return new BridgePlaybackRegion(modification, host);
    }
    void doUpdateMusicalContextContent(ARA::PlugIn::MusicalContext *context,
                                       const ARA::ARAContentTimeRange *,
                                       ARA::ContentUpdateScopes) noexcept override
    {
        static_cast<BridgeMusicalContext *>(context)->refreshTimeline();
    }

    void doUpdateAudioSourceContent(ARA::PlugIn::AudioSource *source,
                                    const ARA::ARAContentTimeRange *,
                                    ARA::ContentUpdateScopes) noexcept override
    {
        static_cast<BridgeAudioSource *>(source)->refreshNotes();
    }

    void willEnableAudioSourceSamplesAccess(ARA::PlugIn::AudioSource *, bool) noexcept override
    {
    }

    void didEnableAudioSourceSamplesAccess(ARA::PlugIn::AudioSource *, bool) noexcept override
    {
    }

    bool doRestoreObjectsFromArchive(
        ARA::PlugIn::HostArchiveReader *reader,
        const ARA::PlugIn::RestoreObjectsFilter *filter) noexcept override
    {
        try
        {
            const auto size = reader->getArchiveSize();
            if (size > (std::numeric_limits<std::int32_t>::max)())
                return false;
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            if (bytes.empty() || !reader->readBytesFromArchive(0, bytes.size(), bytes.data()))
                return false;
            auto bundle = decodeAraArchiveBundle(bytes);
            struct PendingSource
            {
                BridgeAudioSource *source;
                std::vector<std::uint64_t> ids;
                std::uint64_t counter;
            };
            std::vector<PendingSource> pendingSources;
            for (const auto &record : bundle.sources)
            {
                auto *source = filter->getAudioSourceToRestoreStateWithID<BridgeAudioSource>(
                    record.persistentId.c_str());
                if (!source)
                    continue;
                const auto identity = decodeAraSourceIdentity(record.state);
                auto restored = restoreAraSourceIdentity(
                    identity, {source->nextNoteId, source->notes, source->noteIds});
                pendingSources.push_back({source, std::move(restored.ids), restored.nextId});
            }
            struct PendingRestore
            {
                std::shared_ptr<EditorDocument> editor;
                std::vector<std::uint8_t> state;
                std::vector<std::uint8_t> previous;
            };
            std::vector<PendingRestore> pending;
            for (auto &record : bundle.modifications)
            {
                auto *modification =
                    filter->getAudioModificationToRestoreStateWithID<BridgeAudioModification>(
                        record.persistentId.c_str());
                if (!modification)
                    continue;
                auto editor = modification->editorDocument();
                if (!record.state.empty())
                {
                    if (!runtimeReady)
                        runtimeReady = runtime.initialize(managedBundleDirectory());
                    if (!runtimeReady || runtime.validateProjectState(
                                             record.state.data(),
                                             static_cast<std::int32_t>(record.state.size())) != 0)
                        return false;
                }
                std::vector<std::uint8_t> previous;
                if (editor->handle != 0)
                {
                    std::int32_t required = 0;
                    if (editor->runtime.copyDocumentState(editor->handle, nullptr, 0, &required) !=
                        0)
                        return false;
                    previous.resize(required);
                    if (editor->runtime.copyDocumentState(editor->handle, previous.data(), required,
                                                          &required) != 0)
                        return false;
                }
                pending.push_back(
                    {std::move(editor), std::move(record.state), std::move(previous)});
            }
            // Validate and capture every document before replacing any live
            // project. The managed restore detaches/rebinds existing views and
            // suppresses state echoes. Native snapshots commit only on success.
            for (std::size_t index = 0; index < pending.size(); ++index)
            {
                auto &entry = pending[index];
                if (entry.editor->handle == 0)
                    continue;
                if (entry.editor->runtime.restoreDocumentState(
                        entry.editor->handle, entry.state.data(),
                        static_cast<std::int32_t>(entry.state.size())) != 0)
                {
                    // Include the failing document: a UI restore may have
                    // failed after beginning its replacement.
                    for (std::size_t undo = index + 1; undo > 0; --undo)
                    {
                        auto &original = pending[undo - 1];
                        if (original.editor->handle != 0 &&
                            original.editor->runtime.restoreDocumentState(
                                original.editor->handle, original.previous.data(),
                                static_cast<std::int32_t>(original.previous.size())) != 0)
                            OutputDebugStringA("TuneLab: ARA document rollback failed\n");
                    }
                    return false;
                }
            }
            for (auto &entry : pending)
                entry.editor->state = std::move(entry.state);
            for (auto &entry : pendingSources)
            {
                // Host content remains authoritative. Only its association to
                // saved identities changes; do not restore old source events.
                entry.source->noteIds = std::move(entry.ids);
                entry.source->nextNoteId = entry.counter;
                ++entry.source->contentRevision;
            }
            return true;
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return false;
        }
    }

    bool doStoreObjectsToArchive(ARA::PlugIn::HostArchiveWriter *writer,
                                 const ARA::PlugIn::StoreObjectsFilter *filter) noexcept override
    {
        try
        {
            AraArchiveBundle bundle;
            for (const auto *modification :
                 filter->getAudioModificationsToStore<BridgeAudioModification>())
                bundle.modifications.push_back(
                    {modification->getPersistentID(), modification->editorDocument()->state});
            for (const auto *source : filter->getAudioSourcesToStore<BridgeAudioSource>())
                bundle.sources.push_back(
                    {source->getPersistentID(),
                     encodeAraSourceIdentity(
                         {source->nextNoteId, source->notes, source->noteIds})});
            const auto bytes = encodeAraArchiveBundle(bundle);
            return writer->writeBytesToArchive(0, bytes.size(), bytes.data());
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return false;
        }
    }

  private:
    std::vector<std::weak_ptr<std::function<void()>>> hostEditObservers;
    std::vector<std::weak_ptr<std::function<void()>>> modelPollObservers;
    ManagedRuntime runtime;
    bool runtimeReady = false;
};

class ProbeFactoryConfig : public ARA::PlugIn::FactoryConfig
{
  public:
    const char *getFactoryID() const noexcept override
    {
        return "org.tunelab.cubase.probe.factory.1";
    }

    const char *getPlugInName() const noexcept override
    {
        return "TuneLab ARA Bridge Probe";
    }

    const char *getManufacturerName() const noexcept override
    {
        return "TuneLab";
    }

    const char *getInformationURL() const noexcept override
    {
        return "https://github.com/AI-Hobbyist/TuneLab";
    }

    const char *getVersion() const noexcept override
    {
        return "0.1.0";
    }

    ARA::ARAPlaybackTransformationFlags getSupportedPlaybackTransformationFlags()
        const noexcept override
    {
        return ARA::kARAPlaybackTransformationTimestretch;
    }

    const char *getDocumentArchiveID() const noexcept override
    {
        return "org.tunelab.cubase.probe.archive.1";
    }
};

inline const ARA::ARAFactory *getProbeFactory() noexcept
{
    return ARA::PlugIn::PlugInEntry::getPlugInEntry<ProbeFactoryConfig, ProbeDocumentController>()
        ->getFactory();
}

} // namespace TuneLab::Cubase
