#pragma once

#include "TLBridgeProtocol.h"

#include <atomic>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace BridgeVST3 {

// 插件侧的共享内存会话：创建命名文件映射 + 控制块，负责握手字段与心跳。
// 线程模型：init/shutdown/tick 在消息线程（非实时）；process() 只允许调用本类的
// "实时安全"方法（仅写对齐字段，无锁无分配）。
class BridgeSession
{
public:
    BridgeSession() = default;
    ~BridgeSession();

    BridgeSession (const BridgeSession&) = delete;
    BridgeSession& operator= (const BridgeSession&) = delete;

    // 创建/打开映射并初始化控制块（非实时；消息线程）。sessionId 为会话名（如 "default"）。
    bool init (const std::string& sessionId);
    void shutdown();

    bool isCreated() const noexcept { return mMapping != nullptr; }

    // 插件侧心跳（非实时；Timer）：递增 pluginTick。失败（映射已失效）返回 false。
    bool tick();

    // 宿主是否已连接（实时安全：读对齐字段）。
    bool isConnected() const noexcept
    {
        return mControl != nullptr && mControl->connected != 0;
    }

    // 写 DAW 音频配置（实时安全：仅对齐字段写；prepareToPlay 调用）。
    void writeAudioConfig (uint32_t sampleRate, uint32_t blockSize, uint32_t activeBuses, uint32_t latencySamples) noexcept
    {
        if (mControl == nullptr)
            return;

        mControl->sampleRate     = sampleRate;
        mControl->blockSize      = blockSize;
        mControl->activeBuses    = activeBuses;
        mControl->latencySamples = latencySamples;
    }

    // —— M1：实时安全访问器（process() 专用：无锁、无分配、无系统调用）——
    // 环形缓冲按绝对采样位置寻址（帧 p → 槽 p % TL_BRIDGE_RING_SAMPLES）；宿主为写者，
    // 插件为读者。环状态字段（writePos/readPos/underflow）经 std::atomic 自由函数访问
    // （reinterpret_cast 到 atomic<uint64_t>*，对齐安全），与宿主 C# 侧 MemoryBarrier 配对。

    TLBridgeRing* ringState (uint32_t bus) const noexcept
    {
        if (mControl == nullptr || bus >= TL_BRIDGE_MAX_TRACKS)
            return nullptr;
        return reinterpret_cast<TLBridgeRing*> (reinterpret_cast<uint8_t*> (mControl) + TL_BRIDGE_OFF_RING_STATE (bus));
    }

    // 总线 bus 的数据区起始（L/R 交错 float，容量 TL_BRIDGE_RING_SAMPLES 帧）。
    float* ringData (uint32_t bus) const noexcept
    {
        if (mControl == nullptr || bus >= TL_BRIDGE_MAX_TRACKS)
            return nullptr;
        return reinterpret_cast<float*> (reinterpret_cast<uint8_t*> (mControl) + TL_BRIDGE_OFF_RING_DATA (bus));
    }

    // 宿主已渲染到的绝对位置上限（acquire：须在读环数据之前）。
    uint64_t ringWritePos (uint32_t bus) const noexcept
    {
        auto* state = ringState (bus);
        return state == nullptr ? 0
                                : std::atomic_load_explicit (reinterpret_cast<std::atomic<uint64_t>*> (&state->writePos),
                                                             std::memory_order_acquire);
    }

    uint64_t ringReadPos (uint32_t bus) const noexcept
    {
        auto* state = ringState (bus);
        return state == nullptr ? 0
                                : std::atomic_load_explicit (reinterpret_cast<std::atomic<uint64_t>*> (&state->readPos),
                                                             std::memory_order_relaxed);
    }

    // 记录插件已消费到的位置（release）。
    void ringSetReadPos (uint32_t bus, uint64_t pos) noexcept
    {
        auto* state = ringState (bus);
        if (state != nullptr)
            std::atomic_store_explicit (reinterpret_cast<std::atomic<uint64_t>*> (&state->readPos), pos,
                                        std::memory_order_release);
    }

    // 累计下溢样本数（宿主/诊断可读）。
    void ringAddUnderflow (uint32_t bus, uint64_t count) noexcept
    {
        auto* state = ringState (bus);
        if (state != nullptr)
            std::atomic_fetch_add_explicit (reinterpret_cast<std::atomic<uint64_t>*> (&state->underflow), count,
                                            std::memory_order_relaxed);
    }

    // 写 DAW 播放位置（64 位绝对采样，实时安全）。
    void writeSamplePos (uint64_t pos) noexcept
    {
        if (mControl != nullptr)
            mControl->samplePos = pos;
    }

    // 读 DAW 播放位置（诊断/编辑器用，非实时路径）。
    uint64_t readSamplePos() const noexcept
    {
        return mControl != nullptr ? mControl->samplePos : 0;
    }

    // —— M2 传输同步：写 DAW 传输状态（play/loop 位、曲速、拍号、PPQ）——实时安全（仅对齐字段写）。
    void writeTransport (uint64_t state, double tempo, int32_t timeSigNum, int32_t timeSigDen,
                         double ppqPosition, double ppqOfLastBarStart) noexcept
    {
        if (mControl == nullptr)
            return;

        mControl->state             = state;
        mControl->tempo             = tempo;
        mControl->timeSigNum        = timeSigNum;
        mControl->timeSigDen        = timeSigDen;
        mControl->ppqPosition       = ppqPosition;
        mControl->ppqOfLastBarStart = ppqOfLastBarStart;
    }

    uint32_t readSampleRate() const noexcept  { return mControl != nullptr ? mControl->sampleRate : 0; }
    uint32_t readBlockSize() const noexcept   { return mControl != nullptr ? mControl->blockSize : 0; }
    uint32_t readLatencySamples() const noexcept { return mControl != nullptr ? mControl->latencySamples : 0; }
    uint32_t readActiveBuses() const noexcept { return mControl != nullptr ? mControl->activeBuses : 0; }

    bool readTrackRoute (uint32_t track, uint32_t& enabled, uint32_t& busIndex) const noexcept
    {
        if (mControl == nullptr || track >= TL_BRIDGE_MAX_TRACKS)
            return false;

        enabled = mControl->tracks[track].enabled;
        busIndex = mControl->tracks[track].busIndex;
        return true;
    }

    void writeActiveBuses (uint32_t activeBuses) noexcept
    {
        if (mControl != nullptr)
            mControl->activeBuses = activeBuses;
    }

private:
#ifdef _WIN32
    HANDLE mMapping = nullptr;
#endif
    TLBridgeControl* mControl = nullptr;
    uint64_t mPluginTick = 0;
    std::string mSessionId;
};

} // namespace BridgeVST3
