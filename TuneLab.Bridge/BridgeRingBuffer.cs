using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;
using System.Threading;

namespace TuneLab.Bridge;

// 宿主侧共享环形缓冲 I/O（M1）：按绝对采样位置寻址的滑窗（帧 p → 槽 p % RingSamples）。
// 本类仅供宿主（写者）使用：先写环数据，再发布 writePos（release 语义，插件以 acquire 读，
// 见 BridgeVST3Shared.cpp）。写者唯一（渲染线程），无需锁。
[SupportedOSPlatform("windows")]
internal sealed class BridgeRingBuffer
{
    public BridgeRingBuffer(MemoryMappedViewAccessor accessor)
    {
        mAccessor = accessor;
    }

    // 写入 bus 上绝对位置 [startPos, startPos+count) 的交错立体声数据（data 为 L/R 交错，
    // 从 offset 起共 count*2 个 float）。跨环尾自动分段，最多两段。
    public void Write(int bus, long startPos, float[] data, int offset, int count)
    {
        if (count <= 0)
            return;

        long slotStart = startPos % RingSamples;
        int first = (int)Math.Min(count, RingSamples - slotStart);
        WriteAt(bus, slotStart, data, offset, first);
        if (first < count)
            WriteAt(bus, 0, data, offset + first * 2, count - first);
    }

    public ulong GetWritePos(int bus) => mAccessor.ReadUInt64(RingStateStart(bus) + BridgeProtocol.RingOffWritePos);

    // release：确保此前环数据写入对插件可见（与插件 acquire 读 writePos 配对）。
    public void SetWritePos(int bus, ulong pos)
    {
        Thread.MemoryBarrier();
        mAccessor.Write(RingStateStart(bus) + BridgeProtocol.RingOffWritePos, pos);
    }

    public void ResetWritePos(int bus) => mAccessor.Write(RingStateStart(bus) + BridgeProtocol.RingOffWritePos, 0UL);

    public ulong GetReadPos(int bus) => mAccessor.ReadUInt64(RingStateStart(bus) + BridgeProtocol.RingOffReadPos);

    public ulong GetUnderflow(int bus) => mAccessor.ReadUInt64(RingStateStart(bus) + BridgeProtocol.RingOffUnderflow);

    void WriteAt(int bus, long slotStart, float[] data, int offset, int frames)
    {
        long bytePos = BridgeProtocol.RingDataStart(bus) + slotStart * 2 * sizeof(float);
        mAccessor.WriteArray(bytePos, data, offset, frames * 2);
    }

    static int RingStateStart(int bus) => BridgeProtocol.RingStateStart(bus);
    static int RingDataStart(int bus) => BridgeProtocol.RingDataStart(bus);
    static int RingSamples => BridgeProtocol.RingSamples;

    readonly MemoryMappedViewAccessor mAccessor;
}
