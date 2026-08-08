#include "BridgeVST3Processor.h"
#include "BridgeVST3Editor.h"

namespace BridgeVST3 {

namespace {

// 从环形缓冲复制 [from, to) 帧到立体声输出（L/R 分开）。ring 为交错数据区。
// 实时安全：无锁、无分配；按环容量绕回，至多两段。from/to 为绝对采样位置。
void copyRing (const float* ring, uint64_t from, uint64_t to, float* ch0, float* ch1)
{
    uint64_t out = 0;
    uint64_t i = from;
    while (i < to)
    {
        const uint64_t slot = i % TL_BRIDGE_RING_SAMPLES;
        const uint64_t run = juce::jmin<uint64_t> (to - i, TL_BRIDGE_RING_SAMPLES - slot);
        const float* src = ring + slot * 2;
        for (uint64_t k = 0; k < run; ++k)
        {
            ch0[out] = src[k * 2];
            ch1[out] = src[k * 2 + 1];
            ++out;
        }
        i += run;
    }
}

} // namespace

BridgeVST3Processor::BridgeVST3Processor()
    : AudioProcessor (makeBuses())
{
    mSessionId = "default";
    if (mSession.init (mSessionId.toStdString()))
        startTimer (TL_BRIDGE_HEARTBEAT_MS);
}

BridgeVST3Processor::~BridgeVST3Processor()
{
    stopTimer();
    mSession.shutdown();
}

juce::AudioProcessor::BusesProperties BridgeVST3Processor::makeBuses()
{
    // M1：默认激活全部 TL_BRIDGE_MAX_TRACKS 条立体声输出总线，保证 DAW 始终能看到全部输出
    // （空轨总线静音）；动态按轨启用/停用留待 M3。
    auto buses = juce::AudioProcessor::BusesProperties();
    for (int i = 0; i < static_cast<int> (TL_BRIDGE_MAX_TRACKS); ++i)
        buses = buses.withOutput ("Track " + juce::String (i + 1), juce::AudioChannelSet::stereo(), true);

    return buses;
}

void BridgeVST3Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // M1：写 DAW 音频配置；初始延迟取控制块（宿主每帧写 push-ahead 提前量）。
    // 注意：getBusCount 参数是 isInput——输出总线要用 false（插件无输入总线，true 恒为 0）。
    const uint32_t activeBuses = static_cast<uint32_t> (getBusCount (false));
    const uint32_t latency = mSession.readLatencySamples();
    mSession.writeAudioConfig (static_cast<uint32_t> (sampleRate), static_cast<uint32_t> (samplesPerBlock), activeBuses, latency);
    setLatencySamples (static_cast<int> (latency));
    mLastLatency = latency;
}

void BridgeVST3Processor::releaseResources()
{
}

void BridgeVST3Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    // 未连接（或会话未就绪）：输出静音。
    if (!mSession.isCreated() || !mSession.isConnected())
        return;

    // 上报 DAW 播放位置（64 位绝对采样，宿主据此决定渲染到哪里）。
    uint64_t playPos = 0;

    // —— M2 传输同步：上报 DAW 传输状态（play/loop、曲速、拍号、PPQ），宿主据此驱动
    // TuneLab 播放/暂停/播放头跟随与时基覆盖（DAW 为 master）。
    uint64_t stateBits = 0;
    double tempo = 0.0;
    int32_t timeSigNum = 0, timeSigDen = 0;
    double ppqPosition = 0.0, ppqOfLastBarStart = 0.0;
    if (auto* playHead = getPlayHead())
    {
        if (const auto pos = playHead->getPosition())
        {
            if (const auto timeInSamples = pos->getTimeInSamples())
                playPos = static_cast<uint64_t> (juce::jmax<juce::int64> (0, *timeInSamples));

            if (pos->getIsPlaying())
                stateBits |= TL_BRIDGE_STATE_PLAYING;
            if (pos->getIsLooping())
                stateBits |= TL_BRIDGE_STATE_LOOPING;
            if (const auto bpm = pos->getBpm())
                tempo = *bpm;
            if (const auto sig = pos->getTimeSignature())
            {
                timeSigNum = sig->numerator;
                timeSigDen = sig->denominator;
            }
            if (const auto ppq = pos->getPpqPosition())
                ppqPosition = *ppq;
            if (const auto barStart = pos->getPpqPositionOfLastBarStart())
                ppqOfLastBarStart = *barStart;
        }
    }
    mSession.writeSamplePos (playPos);
    mSession.writeTransport (stateBits, tempo, timeSigNum, timeSigDen, ppqPosition, ppqOfLastBarStart);

    // M1 输出侧：DAW 暂停/停止（未播放）时输出静音、不读环——否则暂停在时间轴某处时，
    // 插件会持续读出该处环数据（宿主暂停时还在 idle 回填刷新这段），造成该片段被重复播放。
    // 顶部 buffer.clear() 已保证输出保持静音；播放恢复后照常读环（暂停期间宿主已把该处回填就绪）。
    if ((stateBits & TL_BRIDGE_STATE_PLAYING) == 0)
        return;

    // 逐总线从共享环拉取 [playPos, playPos+numSamples) 填输出总线。
    // 注意：getBusCount 参数是 isInput——输出总线要用 false（插件无输入总线，true 恒为 0，
    // 会导致本循环一次都不执行、输出恒静音）。
    const int numOutputBuses = getBusCount (false);
    for (int bus = 0; bus < numOutputBuses; ++bus)
    {
        const int chIndex = getChannelIndexInProcessBlockBuffer (false, bus, 0);
        if (chIndex < 0 || chIndex + 1 >= numChannels)
            continue;
        float* ch0 = buffer.getWritePointer (chIndex);
        float* ch1 = buffer.getWritePointer (chIndex + 1);

        const float* ring = mSession.ringData (static_cast<uint32_t> (bus));
        const uint64_t writePos = mSession.ringWritePos (static_cast<uint32_t> (bus));
        const uint64_t needEnd = playPos + static_cast<uint64_t> (numSamples);

        if (ring != nullptr && writePos >= needEnd)
        {
            // 全部就绪：直接复制。
            copyRing (ring, playPos, needEnd, ch0, ch1);
        }
        else
        {
            // 下溢：复制已就绪部分，其余补 0 并累计。
            const uint64_t ready = (writePos > playPos) ? (writePos - playPos) : 0;
            if (ready > 0)
                copyRing (ring, playPos, playPos + ready, ch0, ch1);
            for (uint64_t i = ready; i < static_cast<uint64_t> (numSamples); ++i)
            {
                ch0[i] = 0.0f;
                ch1[i] = 0.0f;
            }
            mSession.ringAddUnderflow (static_cast<uint32_t> (bus), static_cast<uint64_t> (numSamples) - ready);
        }

        mSession.ringSetReadPos (static_cast<uint32_t> (bus), needEnd);
    }
}

void BridgeVST3Processor::timerCallback()
{
    if (!mSession.isCreated())
        return;

    mSession.tick();

    const bool connected = mSession.isConnected();
    if (connected != mLastConnected.load())
    {
        mLastConnected.store (connected);
        // 编辑器通过自身 Timer 轮询 lastConnected，无需主动通知。
    }

    // 宿主更新 push-ahead 延迟后同步给 DAW（setLatencySamples 会在运行时重建总线延迟）。
    const uint32_t latency = mSession.readLatencySamples();
    if (latency != mLastLatency)
    {
        mLastLatency = latency;
        setLatencySamples (static_cast<int> (latency));
    }
}

juce::AudioProcessorEditor* BridgeVST3Processor::createEditor()
{
    return new BridgeVST3Editor (*this);
}

} // namespace BridgeVST3

// VST3 包装层入口：返回插件实例（CMake 构建需手动提供，见 juce_AudioProcessor.h）。
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BridgeVST3::BridgeVST3Processor();
}
