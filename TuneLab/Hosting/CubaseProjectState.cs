using System;
using System.Buffers.Binary;
using System.Formats.Cbor;
using System.IO;
using TuneLab.Extensions.Formats.TLP;

namespace TuneLab.Hosting;

// Private VST3/ARA envelope around the existing native project serializer.
// Encode/decode run on the document thread, never the audio callback.
internal static class CubaseProjectState
{
    const int HeaderSize = 16;
    const uint Version = 1;

    public static byte[] Encode(NativeProjectFile project)
    {
        using var payload = new MemoryStream();
        new TuneLabProjectCbor().SerializeNative(payload, project);
        var bytes = payload.ToArray();
        var state = new byte[checked(HeaderSize + bytes.Length)];
        "TLCUBASE"u8.CopyTo(state);
        BinaryPrimitives.WriteUInt32LittleEndian(state.AsSpan(8), Version);
        BinaryPrimitives.WriteInt32LittleEndian(state.AsSpan(12), bytes.Length);
        bytes.CopyTo(state, HeaderSize);
        return state;
    }

    public static NativeProjectFile Decode(ReadOnlySpan<byte> state)
    {
        if (state.Length < HeaderSize || !state[..8].SequenceEqual("TLCUBASE"u8))
        {
            throw new InvalidDataException("Invalid Cubase project state header.");
        }
        if (BinaryPrimitives.ReadUInt32LittleEndian(state[8..]) != Version)
        {
            throw new NotSupportedException("Unsupported Cubase project state version.");
        }
        int length = BinaryPrimitives.ReadInt32LittleEndian(state[12..]);
        if (length <= 0 || length != state.Length - HeaderSize)
        {
            throw new InvalidDataException("Invalid Cubase project state payload length.");
        }
        var payload = state[HeaderSize..].ToArray();
        var reader = new CborReader(payload);
        reader.SkipValue();
        if (reader.BytesRemaining != 0)
        {
            throw new InvalidDataException("Cubase project state contains trailing payload data.");
        }
        using var stream = new MemoryStream(payload, writable: false);
        return new TuneLabProjectCbor().DeserializeNative(stream);
    }
}
