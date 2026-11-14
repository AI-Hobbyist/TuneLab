#include "note_text_ingress.h"
#include "note_text_input.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include <iostream>
#include <limits>
#include <thread>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace TuneLab::Cubase;

// Synthetic events constructed from the public SDK contract. These are not
// recordings from Cubase/VOCALOID and do not establish Key Editor delivery.
int main()
{
    NoteTextInput<4, 8> input;
    TChar text[]{0x4f60, 0x597d, 0xd83d, 0xde00, 0};
    Event event{};
    event.type = Event::kNoteExpressionTextEvent;
    event.sampleOffset = 17;
    event.ppqPosition = 12.5;
    event.noteExpressionText = {kTextTypeID, 42, 4, text};
    EventList list;
    list.addEvent(event);
    if (input.capture(&list, 64, 9) != 1)
        return 1;
    text[0] = '!';
    NoteTextInput<4, 8>::Record record;
    if (!input.pop(record) || record.noteId != 42 || record.sampleOffset != 17 ||
        record.frame != 17 || record.quarter != 12.5 || record.revision != 9 ||
        record.length != 4 || record.text[0] != 0x4f60 || record.text[2] != 0xd83d ||
        record.text[3] != 0xde00 || record.text[4] != 0 || input.pop(record))
        return 2;

    // Offset-zero flush with empty text clears a lyric; it does not advance the clock.
    list.clear();
    event.sampleOffset = 0;
    event.noteExpressionText = {kTextTypeID, 42, 0, nullptr};
    list.addEvent(event);
    if (input.capture(&list, 0) != 1 || !input.pop(record) || record.length != 0 ||
        record.text[0] != 0 || record.frame != 64)
        return 3;

    auto rejected = [&](Event candidate) {
        list.clear();
        list.addEvent(candidate);
        return input.capture(&list, 64) == 0 && !input.pop(record);
    };
    event.noteExpressionText = {kTextTypeID, -1, 0, nullptr};
    if (!rejected(event))
        return 4;
    event.noteExpressionText = {kTextTypeID, 42, 1, nullptr};
    if (!rejected(event))
        return 5;
    event.noteExpressionText = {kTextTypeID, 42, 9, text};
    if (!rejected(event))
        return 6;
    TChar high[]{0xd83d, 0};
    event.noteExpressionText = {kTextTypeID, 42, 1, high};
    if (!rejected(event))
        return 7;
    TChar low[]{0xde00, 0};
    event.noteExpressionText.text = low;
    if (!rejected(event))
        return 8;
    TChar embeddedNull[]{'a', 0, 'b', 0};
    event.noteExpressionText = {kTextTypeID, 42, 3, embeddedNull};
    if (!rejected(event))
        return 9;
    event.noteExpressionText = {kTextTypeID, 42, 0, nullptr};
    event.sampleOffset = 64;
    if (!rejected(event))
        return 10;
    event.sampleOffset = 0;
    event.ppqPosition = std::numeric_limits<double>::quiet_NaN();
    if (!rejected(event) || input.rejectedEvents() != 8)
        return 11;
    event.ppqPosition = 0;
    event.noteExpressionText.typeId = kPhonemeTypeID;
    if (!rejected(event) || input.rejectedEvents() != 8)
        return 12;

    event.noteExpressionText.typeId = kTextTypeID;
    list.clear();
    list.addEvent(event);
    if (input.capture(&list, 64) != 1 || input.capture(&list, 64) != 1 ||
        input.capture(&list, 64) != 1 || input.capture(&list, 64) != 0 || input.lostEvents() != 1)
        return 13;
    input.discardPending();
    if (input.pop(record))
        return 14;

    NoteTextInput<> concurrent;
    constexpr int count = 100000;
    std::thread producer([&] {
        EventList source;
        TChar payload[]{'a', 0};
        Event current{};
        current.type = Event::kNoteExpressionTextEvent;
        current.noteExpressionText = {kTextTypeID, 0, 1, payload};
        for (int index = 0; index < count; ++index)
        {
            payload[0] = static_cast<TChar>('a' + index % 26);
            current.noteExpressionText.noteId = index;
            source.clear();
            source.addEvent(current);
            while (concurrent.capture(&source, 64, 123) == 0)
                std::this_thread::yield();
            payload[0] = '!';
        }
    });
    bool coherent = true;
    for (int index = 0; index < count; ++index)
    {
        NoteTextInput<>::Record current;
        while (!concurrent.pop(current))
            std::this_thread::yield();
        coherent = coherent && current.noteId == index && current.revision == 123 &&
                   current.length == 1 && current.text[0] == static_cast<TChar>('a' + index % 26) &&
                   current.text[1] == 0;
    }
    producer.join();
    if (!coherent)
        return 15;
    {
        NoteTextIngress ingress;
        ProcessContext context{};
        context.state = ProcessContext::kPlaying;
        Event on{};
        on.type = Event::kNoteOnEvent;
        on.ppqPosition = 8;
        on.noteOn = {0, 60, 0, 1, 0, 42};
        Event lyric{};
        lyric.type = Event::kNoteExpressionTextEvent;
        lyric.ppqPosition = 8;
        lyric.noteExpressionText = {kTextTypeID, 42, 0, nullptr};
        NoteTextIngress::Record captured;
        auto block = [&](std::initializer_list<Event> events, std::uint64_t revision = 1) {
            EventList batch;
            for (auto event : events)
                batch.addEvent(event);
            ingress.capture(&batch, 64, &context, revision);
            context.projectTimeSamples += 64;
        };
        // Same-offset text-before-on and a later block both refer to onset 8.
        block({lyric, on});
        if (!ingress.input.pop(captured) || captured.pitch != 60 || captured.onsetQuarter != 8)
            return 16;
        lyric.ppqPosition = 9;
        block({lyric});
        if (!ingress.input.pop(captured) || captured.pitch != 60 || captured.onsetQuarter != 8)
            return 17;
        context.projectTimeSamples = 0;
        block({lyric});
        if (!ingress.input.pop(captured) || captured.pitch != -1)
            return 18;
        block({on, lyric});
        if (!ingress.input.pop(captured) || captured.pitch != 60)
            return 19;
        block({lyric}, 2);
        if (!ingress.input.pop(captured) || captured.pitch != -1)
            return 20;
        Event off{};
        off.type = Event::kNoteOffEvent;
        off.noteOff = {0, 60, 0, 42, 0};
        block({on, off, lyric}, 2);
        if (!ingress.input.pop(captured) || captured.pitch != -1)
            return 21;
        on.flags = Event::kIsLive;
        block({on, lyric}, 2);
        if (!ingress.input.pop(captured) || captured.pitch != -1)
            return 22;
        on.flags = 0;
        block({on, on, lyric}, 2);
        if (!ingress.input.pop(captured) || captured.pitch != -1)
            return 23;
        std::cout << "SDK runtime association: onset/pitch, same-offset text ordering, cross-block "
                     "binding, seek/revision/off reset, live and duplicate-ID rejection passed\n";
    }
    std::cout << "SDK synthetic Note Expression text: owned UTF-16, empty/invalid payloads, "
                 "overflow/discard and 100000 concurrent transfers passed (no GUI)\n";
    return 0;
}
