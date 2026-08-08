using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;
using TuneLab.Bridge;
using Xunit;

namespace TuneLab.Tests.Bridge;

// 宿主侧环形缓冲（BridgeRingBuffer）行为测试：进程内共享内存模拟映射，无需插件。
[SupportedOSPlatform("windows")]
public class BridgeRingBufferTests : IDisposable
{
    public BridgeRingBufferTests()
    {
        mName = "TuneLab.Tests.Ring." + Guid.NewGuid().ToString("N");
        mMapping = MemoryMappedFile.CreateNew(mName, BridgeProtocol.TotalSize);
        mAccessor = mMapping.CreateViewAccessor(0, BridgeProtocol.TotalSize, MemoryMappedFileAccess.ReadWrite);
        mRing = new BridgeRingBuffer(mAccessor);
    }

    public void Dispose()
    {
        mAccessor.Dispose();
        mMapping.Dispose();
    }

    [Fact]
    public void WritePosStartsZero()
    {
        Assert.Equal(0UL, mRing.GetWritePos(0));
        Assert.Equal(0UL, mRing.GetWritePos(63));
    }

    [Fact]
    public void WriteAndPublishDataIsVisible()
    {
        const int bus = 3;
        const int frames = 1000;
        var data = new float[frames * 2];
        for (int i = 0; i < frames; i++)
        {
            data[i * 2] = i;          // L
            data[i * 2 + 1] = -i;     // R
        }

        mRing.Write(bus, 0, data, 0, frames);
        mRing.SetWritePos(bus, (ulong)frames);

        Assert.Equal((ulong)frames, mRing.GetWritePos(bus));

        // 逐帧校验环数据区（slot == 绝对位置，因写从 0 起）。
        for (int i = 0; i < frames; i++)
        {
            long bytePos = BridgeProtocol.RingDataStart(bus) + (long)i * 2 * sizeof(float);
            Assert.Equal(i, mAccessor.ReadSingle(bytePos));
            Assert.Equal(-i, mAccessor.ReadSingle(bytePos + sizeof(float)));
        }
    }

    [Fact]
    public void WriteSpansRingWrap()
    {
        const int bus = 0;
        // 写位置紧贴环尾：从 RingSamples-10 写 20 帧 → 前 10 帧在尾、后 10 帧绕到环头。
        const int start = BridgeProtocol.RingSamples - 10;
        const int frames = 20;
        var data = new float[frames * 2];
        for (int i = 0; i < frames; i++)
            data[i * 2] = 1000 + i;   // L 唯一标记

        mRing.Write(bus, start, data, 0, frames);
        mRing.SetWritePos(bus, (ulong)(start + frames));

        // 环头：slot 0..9 ← 帧 10..19
        for (int i = 0; i < 10; i++)
        {
            long bytePos = BridgeProtocol.RingDataStart(bus) + (long)i * 2 * sizeof(float);
            Assert.Equal(1000 + 10 + i, mAccessor.ReadSingle(bytePos));
        }
        // 环尾：slot (start..RingSamples-1) ← 帧 0..9
        for (int i = 0; i < 10; i++)
        {
            long bytePos = BridgeProtocol.RingDataStart(bus) + (long)(start + i) * 2 * sizeof(float);
            Assert.Equal(1000 + i, mAccessor.ReadSingle(bytePos));
        }
    }

    [Fact]
    public void SeekResetSetsWritePos()
    {
        mRing.SetWritePos(5, 12345UL);
        Assert.Equal(12345UL, mRing.GetWritePos(5));
        mRing.ResetWritePos(5);
        Assert.Equal(0UL, mRing.GetWritePos(5));
    }

    [Fact]
    public void BusesDoNotOverlap()
    {
        var a = new float[2] { 1f, 2f };
        var b = new float[2] { 3f, 4f };
        mRing.Write(0, 0, a, 0, 1);
        mRing.Write(1, 0, b, 0, 1);

        long pos0 = BridgeProtocol.RingDataStart(0);
        long pos1 = BridgeProtocol.RingDataStart(1);
        Assert.Equal(1f, mAccessor.ReadSingle(pos0));
        Assert.Equal(2f, mAccessor.ReadSingle(pos0 + 4));
        Assert.Equal(3f, mAccessor.ReadSingle(pos1));
        Assert.Equal(4f, mAccessor.ReadSingle(pos1 + 4));
    }

    readonly string mName;
    readonly MemoryMappedFile mMapping;
    readonly MemoryMappedViewAccessor mAccessor;
    readonly BridgeRingBuffer mRing;
}
