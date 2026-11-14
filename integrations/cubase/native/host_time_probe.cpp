#include "host_time.h"
#include <iostream>
#include <thread>

using namespace TuneLab::Cubase;
using Context = Steinberg::Vst::ProcessContext;

Context contextAt(int position)
{
    Context context{};
    context.state = Context::kPlaying | Context::kProjectTimeMusicValid | Context::kTempoValid |
                    Context::kTimeSigValid | Context::kBarPositionValid;
    context.sampleRate = 48000;
    context.projectTimeSamples = position;
    context.projectTimeMusic = position * 0.25;
    context.tempo = 90 + position % 100;
    context.timeSigNumerator = 1 + position % 7;
    context.timeSigDenominator = 8;
    context.barPositionMusic = position;
    return context;
}

bool coherent(const HostTimeSnapshot &snapshot)
{
    const auto position = snapshot.samplePosition;
    return snapshot.sampleRate == 48000 && snapshot.quarterNotes == position * 0.25 &&
           snapshot.tempo == 90 + position % 100 && snapshot.numerator == 1 + position % 7 &&
           snapshot.denominator == 8 && snapshot.barQuarterNotes == position;
}

int main()
{
    HostTimeMailbox mailbox;
    auto context = contextAt(8);
    context.projectTimeSamples = -48000;
    context.projectTimeMusic = -2.5;
    mailbox.publish(&context);
    HostTimeSnapshot snapshot;
    if (!mailbox.tryRead(snapshot) || snapshot.samplePosition != -48000 ||
        snapshot.quarterNotes != -2.5 || snapshot.numerator != 2 || snapshot.denominator != 8)
        return 1;
    context.state = 0;
    mailbox.publish(&context);
    if (!mailbox.tryRead(snapshot) || !std::isnan(snapshot.quarterNotes) ||
        !std::isnan(snapshot.tempo) || snapshot.numerator != 0 || snapshot.denominator != 0)
        return 2;
    context.state = Context::kTempoValid | Context::kTimeSigValid | Context::kProjectTimeMusicValid;
    context.tempo = -1;
    context.timeSigDenominator = 0;
    context.projectTimeMusic = std::numeric_limits<double>::infinity();
    mailbox.publish(&context);
    if (!mailbox.tryRead(snapshot) || snapshot.flags != 0 || !std::isnan(snapshot.tempo) ||
        !std::isnan(snapshot.quarterNotes))
        return 3;
    mailbox.publish(nullptr);
    if (!mailbox.tryRead(snapshot) || snapshot.flags != 0 || snapshot.sampleRate != 0)
        return 4;
    context = contextAt(0);
    mailbox.publish(&context);
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::thread writer([&] {
        while (reads.load() == 0)
            std::this_thread::yield();
        for (int position = 1; position <= 100000; ++position)
        {
            auto next = contextAt(position);
            mailbox.publish(&next);
        }
        stop.store(true);
    });
    bool valid = true;
    while (!stop.load())
    {
        if (mailbox.tryRead(snapshot))
        {
            valid = valid && coherent(snapshot);
            reads.fetch_add(1);
        }
    }
    writer.join();
    if (!valid || reads.load() == 0 || !mailbox.tryRead(snapshot) || !coherent(snapshot))
        return 5;
    std::cout << "Host time: signed positions, validity reset, malformed values and 100000 "
                 "coherent concurrent publications passed\n";
    return 0;
}
