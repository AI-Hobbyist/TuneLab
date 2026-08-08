/*
 * TuneLab Bridge 共享协议（唯一真源）
 *
 * 插件侧（Bridge_VST3，C++）与宿主侧（TuneLab.Bridge，C#）共用的共享内存布局与常量。
 * C# 镜像见 TuneLab.Bridge/BridgeProtocol.cs；字段偏移由
 * tests/TuneLab.Tests/Bridge/BridgeProtocolLayoutTests.cs 对照本文件 TL_BRIDGE_OFF_* 宏守护，
 * 避免两侧手改漂移。改动本文件时务必同步 C# 侧与测试。
 *
 * 共享内存：命名文件映射 "TuneLab.Bridge.<session-id>"（Windows Local\ 命名空间）。
 *   单文件映射布局：
 *     [0 .. TL_BRIDGE_CONTROL_SIZE)   控制块（M0 起用）
 *     （后续里程碑在控制块之后追加每轨音频环形缓冲）
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TL_BRIDGE_MAGIC            0x544C4252u   /* "TLBR" */
#define TL_BRIDGE_VERSION          1u
#define TL_BRIDGE_MAX_TRACKS       64u
#define TL_BRIDGE_SESSION_NAME_MAX 128u
#define TL_BRIDGE_TRACK_NAME_MAX   64u

/* 共享内存命名空间前缀：Local\TuneLab.Bridge.<session-id> */
#define TL_BRIDGE_SHM_PREFIX       "TuneLab.Bridge."

/* 心跳（毫秒）：两侧各自递增自己的 tick；对侧 tick 停滞超过超时判为断开 */
#define TL_BRIDGE_HEARTBEAT_MS          500
#define TL_BRIDGE_HEARTBEAT_TIMEOUT_MS  3000

/* 协议错误码 */
enum TLBridgeProtocolError {
    kTLBridgeErrorNone            = 0,
    kTLBridgeErrorMagicMismatch   = 1,
    kTLBridgeErrorVersionMismatch = 2,
    kTLBridgeErrorBusy            = 3,   /* 已有宿主连接 */
};

typedef struct TLBridgeTrack {
    char     name[TL_BRIDGE_TRACK_NAME_MAX];  /* UTF-8 轨道名；空串 = 未占用 */
    uint32_t enabled;                          /* 该轨是否输出 */
    uint32_t busIndex;                         /* 自由分配：本轨 → 输出总线 */
    uint32_t followGainPan;                    /* 1 = 带轨音量/声像，0 = 原始信号 */
    uint32_t mirrorMuteSolo;                   /* 镜像静音/独奏 */
} TLBridgeTrack;

typedef struct TLBridgeControl {
    uint32_t magic;
    uint32_t version;
    uint32_t connected;       /* 宿主握手成功后置 1；断开清零 */
    uint32_t protocolError;   /* 协议错误码 */

    /* —— 传输（插件 → 宿主；每 process() 更新） —— */
    uint64_t samplePos;
    uint64_t state;           /* VST ProcessContext.state 位标志 */
    double   tempo;           /* BPM */
    int32_t  timeSigNum;      /* 拍号分子 */
    int32_t  timeSigDen;      /* 拍号分母 */
    double   ppqPosition;
    double   ppqOfLastBarStart;

    /* —— 音频配置（插件 → 宿主） —— */
    uint32_t sampleRate;
    uint32_t blockSize;
    uint32_t activeBuses;
    uint32_t latencySamples;

    /* —— 轨道表（双向） —— */
    TLBridgeTrack tracks[TL_BRIDGE_MAX_TRACKS];

    /* —— 心跳 —— */
    uint64_t hostTick;
    uint64_t pluginTick;

    /* —— 会话信息 —— */
    char     sessionName[TL_BRIDGE_SESSION_NAME_MAX];
    uint32_t hostPid;
    uint32_t pluginPid;
    uint32_t hostAppVersion;  /* 例 1.6.0 → 0x00010600 */
    uint32_t reserved;
} TLBridgeControl;

/* —— 布局偏移（TLBridgeControl 各字段字节偏移；由布局一致性测试对照 C# 侧校验） —— */
#define TL_BRIDGE_OFF_MAGIC              0u
#define TL_BRIDGE_OFF_VERSION            4u
#define TL_BRIDGE_OFF_CONNECTED          8u
#define TL_BRIDGE_OFF_PROTOCOL_ERROR     12u
#define TL_BRIDGE_OFF_SAMPLE_POS         16u
#define TL_BRIDGE_OFF_STATE              24u
#define TL_BRIDGE_OFF_TEMPO              32u
#define TL_BRIDGE_OFF_TIME_SIG_NUM       40u
#define TL_BRIDGE_OFF_TIME_SIG_DEN       44u
#define TL_BRIDGE_OFF_PPQ_POSITION       48u
#define TL_BRIDGE_OFF_PPQ_BAR_START      56u
#define TL_BRIDGE_OFF_SAMPLE_RATE        64u
#define TL_BRIDGE_OFF_BLOCK_SIZE         68u
#define TL_BRIDGE_OFF_ACTIVE_BUSES       72u
#define TL_BRIDGE_OFF_LATENCY_SAMPLES    76u
#define TL_BRIDGE_OFF_TRACKS             80u
#define TL_BRIDGE_OFF_HOST_TICK          5200u
#define TL_BRIDGE_OFF_PLUGIN_TICK        5208u
#define TL_BRIDGE_OFF_SESSION_NAME       5216u
#define TL_BRIDGE_OFF_HOST_PID           5344u
#define TL_BRIDGE_OFF_PLUGIN_PID         5348u
#define TL_BRIDGE_OFF_HOST_APP_VERSION   5352u
#define TL_BRIDGE_OFF_RESERVED           5356u
#define TL_BRIDGE_CONTROL_SIZE           5360u

/* TLBridgeTrack 布局偏移 */
#define TL_BRIDGE_TRACK_SIZE             80u
#define TL_BRIDGE_TRACK_OFF_NAME         0u
#define TL_BRIDGE_TRACK_OFF_ENABLED      64u
#define TL_BRIDGE_TRACK_OFF_BUS_INDEX    68u
#define TL_BRIDGE_TRACK_OFF_FOLLOW_GAIN_PAN 72u
#define TL_BRIDGE_TRACK_OFF_MIRROR_MUTE_SOLO 76u

#ifdef __cplusplus
} /* extern "C" */
#endif
