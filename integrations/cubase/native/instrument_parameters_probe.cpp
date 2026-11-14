#include "instrument_parameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <iostream>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace TuneLab::Cubase;

int main()
{
    MemoryStream stream;
    bool bypass = false;
    if (writeInstrumentParameters(&stream, true) != kResultOk)
        return 1;
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    if (readInstrumentParameters(&stream, bypass) != kResultOk || !bypass)
        return 2;
    MemoryStream legacy;
    if (readInstrumentParameters(&legacy, bypass) != kResultOk || bypass)
        return 3;
    auto *bytes = stream.getData();
    bytes[7] = '3';
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    if (readInstrumentParameters(&stream, bypass) == kResultOk || bypass)
        return 4;
    bytes[7] = '1';
    MemoryStream truncated(bytes, 11);
    if (readInstrumentParameters(&truncated, bypass) == kResultOk)
        return 5;

    MemoryStream paired;
    bool requiresPair = false;
    if (writeInstrumentParameters(&paired, true, true) != kResultOk)
        return 10;
    paired.seek(0, IBStream::kIBSeekSet, nullptr);
    if (readInstrumentParameters(&paired, bypass, requiresPair) != kResultOk || !bypass ||
        !requiresPair)
        return 11;
    auto *pairedBytes = paired.getData();
    pairedBytes[9] = 2;
    paired.seek(0, IBStream::kIBSeekSet, nullptr);
    if (readInstrumentParameters(&paired, bypass, requiresPair) == kResultOk || !bypass ||
        !requiresPair)
        return 12;
    pairedBytes[9] = 1;
    pairedBytes[7] = '1';
    paired.seek(0, IBStream::kIBSeekSet, nullptr);
    if (readInstrumentParameters(&paired, bypass, requiresPair) == kResultOk)
        return 13;
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    if (readInstrumentParameters(&stream, bypass, requiresPair) != kResultOk || !bypass ||
        requiresPair)
        return 14;
    requiresPair = true;
    if (readInstrumentParameters(&legacy, bypass, requiresPair) != kResultOk || bypass ||
        requiresPair)
        return 15;

    InstrumentParameters parameters;
    std::array<float, 8> left, right;
    left.fill(1);
    right.fill(1);
    float *channels[]{left.data(), right.data()};
    AudioBusBuffers output{};
    output.numChannels = 2;
    output.channelBuffers32 = channels;
    ParameterChanges changes;
    int32 index = 0;
    auto *queue = changes.addParameterData(bypassParameterId, index);
    queue->addPoint(3, 0, index);
    queue->addPoint(4, 1, index);
    queue->addPoint(5, 1, index);
    queue->addPoint(6, 0, index);
    ProcessData data{};
    data.numSamples = 8;
    data.symbolicSampleSize = kSample32;
    data.numOutputs = 1;
    data.outputs = &output;
    data.inputParameterChanges = &changes;
    parameters.process(data);
    for (int sample = 0; sample < 8; ++sample)
        if (left[sample] != (sample == 4 || sample == 5 ? 0 : 1) || left[sample] != right[sample])
            return 6;
    if (parameters.bypassed())
        return 7;
    changes.clearQueue();
    queue = changes.addParameterData(bypassParameterId, index);
    queue->addPoint(0, 1, index);
    data.numSamples = 0;
    data.numOutputs = 0;
    parameters.process(data);
    if (!parameters.bypassed())
        return 8;
    data.numSamples = 8;
    data.numOutputs = 1;
    data.inputParameterChanges = nullptr;
    left.fill(1);
    right.fill(1);
    parameters.process(data);
    if (output.silenceFlags != 3 ||
        std::any_of(left.begin(), left.end(), [](auto value) { return value != 0; }))
        return 9;
    std::cout << "Instrument parameters: versioned/legacy state, malformed preservation, "
                 "sample-accurate bypass and no-output flush passed\n";
    return 0;
}
