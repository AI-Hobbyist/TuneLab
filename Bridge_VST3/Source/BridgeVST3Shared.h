#pragma once

#include "TLBridgeProtocol.h"

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

private:
#ifdef _WIN32
    HANDLE mMapping = nullptr;
#endif
    TLBridgeControl* mControl = nullptr;
    uint64_t mPluginTick = 0;
    std::string mSessionId;
};

} // namespace BridgeVST3
