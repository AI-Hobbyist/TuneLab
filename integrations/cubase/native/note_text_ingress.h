#pragma once

#include "note_text_input.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <limits>

namespace TuneLab::Cubase
{
// Audio-thread-owned runtime association. Only owned text records cross to the
// consumer. A missing/ambiguous association never becomes a guessed document ID.
class NoteTextIngress
{
  public:
    using Record = NoteTextInput<>::Record;
    NoteTextInput<> input;

    void capture(Steinberg::Vst::IEventList *events, std::int32_t samples,
                 const Steinberg::Vst::ProcessContext *context, std::uint64_t revision) noexcept
    {
        if (samples < 0)
            return;
        const bool discontinuity =
            !context || !hasTimeline || context->projectTimeSamples != expectedSample ||
            revision != lastRevision ||
            ((context->state & Steinberg::Vst::ProcessContext::kPlaying) != playing);
        if (discontinuity)
            notes.fill({});
        blockEvents = events;
        blockSamples = samples;
        hasTimeline = context != nullptr;
        if (context)
        {
            expectedSample = context->projectTimeSamples + samples;
            playing = context->state & Steinberg::Vst::ProcessContext::kPlaying;
        }
        lastRevision = revision;
        input.captureWith(events, samples, revision, *this);
        blockEvents = nullptr;
    }

    void observe(const Steinberg::Vst::Event &event) noexcept
    {
        using namespace Steinberg::Vst;
        if (!validPosition(event))
            return;
        if (event.type == Event::kNoteOffEvent && event.noteOff.noteId >= 0)
        {
            for (auto &note : notes)
                if (note.id == event.noteOff.noteId)
                    note = {};
        }
        if (event.type != Event::kNoteOnEvent || !validNote(event))
            return;
        for (auto &note : notes)
        {
            if (note.id == event.noteOn.noteId)
            {
                // Reusing an active ID loses unique association until Note Off.
                note.pitch = -1;
                return;
            }
        }
        for (auto &note : notes)
        {
            if (note.id < 0)
            {
                note = {event.noteOn.noteId, event.noteOn.pitch, event.ppqPosition};
                return;
            }
        }
    }

    void associate(Record &record, const Steinberg::Vst::Event &event,
                   Steinberg::int32 eventIndex) noexcept
    {
        // Project lyrics need a timeline. Live keyboard input is audition data.
        if (!hasTimeline || (event.flags & Steinberg::Vst::Event::kIsLive))
            return;
        for (const auto &note : notes)
        {
            if (note.id == record.noteId)
            {
                record.pitch = note.pitch;
                record.onsetQuarter = note.quarter;
                return;
            }
        }
        // Text may precede its Note On at the same sample offset. Resolve
        // that unordered group only if it contains exactly one valid Note On.
        int matches = 0;
        for (Steinberg::int32 index = eventIndex + 1;
             blockEvents && index < blockEvents->getEventCount(); ++index)
        {
            Steinberg::Vst::Event candidate{};
            if (blockEvents->getEvent(index, candidate) != Steinberg::kResultOk ||
                candidate.type != Steinberg::Vst::Event::kNoteOnEvent ||
                candidate.noteOn.noteId != record.noteId ||
                candidate.sampleOffset != event.sampleOffset || !validPosition(candidate) ||
                !validNote(candidate))
                continue;
            ++matches;
            record.pitch = candidate.noteOn.pitch;
            record.onsetQuarter = candidate.ppqPosition;
        }
        if (matches != 1)
            record.pitch = -1;
    }

  private:
    bool validPosition(const Steinberg::Vst::Event &event) const noexcept
    {
        return event.busIndex == 0 && event.sampleOffset >= 0 &&
               (blockSamples == 0 ? event.sampleOffset == 0 : event.sampleOffset < blockSamples) &&
               std::isfinite(event.ppqPosition);
    }
    static bool validNote(const Steinberg::Vst::Event &event) noexcept
    {
        return event.noteOn.noteId >= 0 && event.noteOn.pitch >= 0 && event.noteOn.pitch <= 127 &&
               event.noteOn.channel >= 0 && event.noteOn.channel < 16 &&
               !(event.flags & Steinberg::Vst::Event::kIsLive);
    }
    struct Note
    {
        Steinberg::int32 id = -1, pitch = -1;
        double quarter = 0;
    };
    std::array<Note, 128> notes{};
    Steinberg::Vst::IEventList *blockEvents = nullptr;
    std::int32_t blockSamples = 0;
    Steinberg::Vst::TSamples expectedSample = 0;
    std::uint64_t lastRevision = 0;
    bool hasTimeline = false;
    Steinberg::uint32 playing = 0;
};
} // namespace TuneLab::Cubase
