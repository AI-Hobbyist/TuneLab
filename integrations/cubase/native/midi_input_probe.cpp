#include "midi_input.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/vsteventshelper.h"
#include <iostream>
#include <thread>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace TuneLab::Cubase;

int main()
{
    MidiInput<8> queue;
    EventList list;
    Event on{};
    Helpers::init(on, Event::kNoteOnEvent, 0, 17, 0, Event::kIsLive);
    on.noteOn = {3, 67, 12.5f, 0.75f, 0, 42};
    list.addEvent(on);
    Event off{};
    Helpers::init(off, Event::kNoteOffEvent, 0, 63);
    off.noteOff = {3, 67, 0.25f, 42, 12.5f};
    list.addEvent(off);
    queue.capture(&list, 64);
    MidiNoteEvent event;
    if (!queue.pop(event) || !event.on || event.channel != 3 || event.pitch != 67 ||
        event.noteId != 42 || event.velocity != 0.75f || event.tuning != 12.5f ||
        event.sampleOffset != 17 || event.frame != 17 || event.flags != Event::kIsLive)
    {
        return 1;
    }
    if (!queue.pop(event) || event.on || event.noteId != 42 || event.frame != 63 ||
        queue.pop(event))
    {
        return 2;
    }
    // Stopped-host blocks still advance the preview clock. Zero-sample flushes
    // carry offset-zero events without advancing it.
    list.clear();
    on.sampleOffset = 0;
    on.noteOn.noteId = -1;
    list.addEvent(on);
    queue.capture(nullptr, 128);
    queue.capture(&list, 0);
    if (!queue.pop(event) || event.frame != 192 || event.noteId != -1)
    {
        return 3;
    }
    list.clear();
    on.sampleOffset = 64;
    list.addEvent(on);
    on.sampleOffset = -1;
    list.addEvent(on);
    on.sampleOffset = 0;
    on.busIndex = 1;
    list.addEvent(on);
    queue.capture(&list, 64);
    if (queue.pop(event))
    {
        return 4;
    }
    for (int index = 0; index < 7; ++index)
    {
        event.noteId = index;
        if (!queue.push(event))
            return 5;
    }
    if (queue.push(event) || queue.lostEvents() != 1)
    {
        return 6;
    }
    queue.discardPending();
    if (queue.pop(event) || !queue.push(event) || !queue.pop(event))
    {
        return 7;
    }

    // Exercise actual producer/consumer concurrency and wrap-around. Retry
    // rejected sends here only; the audio path never waits for queue space.
    MidiInput<64> concurrent;
    constexpr int count = 100000;
    std::thread producer([&] {
        for (int index = 0; index < count; ++index)
        {
            MidiNoteEvent item;
            item.noteId = index;
            item.frame = static_cast<std::uint64_t>(index) * 17;
            while (!concurrent.push(item))
                std::this_thread::yield();
        }
    });
    bool ordered = true;
    for (int index = 0; index < count; ++index)
    {
        while (!concurrent.pop(event))
            std::this_thread::yield();
        ordered = ordered && event.noteId == index &&
                  event.frame == static_cast<std::uint64_t>(index) * 17;
    }
    producer.join();
    if (!ordered)
    {
        return 8;
    }
    MidiHeldNotes<2> held;
    MidiNoteEvent first;
    first.on = true;
    first.channel = 2;
    first.pitch = 60;
    first.noteId = 100;
    first.frame = 10;
    auto second = first;
    second.noteId = 101;
    second.frame = 20;
    if (!held.apply(first) || !held.apply(second))
    {
        return 9;
    }
    auto release = first;
    release.on = false;
    release.channel = 3;
    if (held.apply(release) || !held.notes()[0].active || !held.notes()[1].active)
    {
        return 10;
    }
    release.channel = 2;
    release.pitch = 72; // Explicit ID, not pitch, identifies the released note.
    if (!held.apply(release) || held.notes()[0].active || !held.notes()[1].active)
    {
        return 11;
    }
    held.clear();
    first.noteId = second.noteId = -1;
    held.apply(first);
    held.apply(second);
    if (held.apply(first)) // Capacity is explicit; no silent voice stealing.
    {
        return 12;
    }
    release = second;
    release.on = false;
    if (!held.apply(release) || held.notes()[0].active || !held.notes()[1].active ||
        !held.apply(release) || held.notes()[1].active || held.apply(release))
    {
        return 13;
    }
    held.clear();
    second.frame = first.frame;
    held.apply(first);
    held.apply(second);
    held.apply(release);
    held.apply(first); // Reuse slot zero at the same sample position.
    if (!held.apply(release) || !held.notes()[0].active || held.notes()[1].active)
    {
        return 14;
    }
    std::cout << "MIDI note fields, offsets, flush, overflow recovery and 100000 concurrent "
                 "transfers, polyphonic note identity and release pairing passed\n";
    return 0;
}
