using System;
using System.Buffers.Binary;
using System.Formats.Cbor;
using System.IO;
using System.Text.Json;
using TuneLab.Extensions.Formats.TLP;

namespace TuneLab.Hosting;

// Private VST3/ARA envelope around the existing native project serializer.
// Encode/decode run on the document thread, never the audio callback.
internal static class CubaseProjectState
{
    const int HeaderSize = 16;
    const uint Version = 1;

    public static byte[] Encode(NativeProjectFile project, CubaseAraMetadata? ara = null)
    {
        using var payload = new MemoryStream();
        new TuneLabProjectCbor().SerializeNative(payload, project);
        var bytes = payload.ToArray();
        uint version = Version;
        if (ara != null)
        {
            ara.Validate(project);
            var writer = new CborWriter();
            writer.WriteStartArray(2);
            writer.WriteByteString(bytes);
            writer.WriteByteString(JsonSerializer.SerializeToUtf8Bytes(ara));
            writer.WriteEndArray();
            bytes = writer.Encode();
            version = 2;
        }
        var state = new byte[checked(HeaderSize + bytes.Length)];
        "TLCUBASE"u8.CopyTo(state);
        BinaryPrimitives.WriteUInt32LittleEndian(state.AsSpan(8), version);
        BinaryPrimitives.WriteInt32LittleEndian(state.AsSpan(12), bytes.Length);
        bytes.CopyTo(state, HeaderSize);
        return state;
    }

    public static NativeProjectFile Decode(ReadOnlySpan<byte> state)
        => DecodeEnvelope(state).Project;

    public static CubaseProjectEnvelope DecodeEnvelope(ReadOnlySpan<byte> state)
    {
        if (state.Length < HeaderSize || !state[..8].SequenceEqual("TLCUBASE"u8))
        {
            throw new InvalidDataException("Invalid Cubase project state header.");
        }
        uint version = BinaryPrimitives.ReadUInt32LittleEndian(state[8..]);
        if (version != Version && version != 2)
        {
            throw new NotSupportedException("Unsupported Cubase project state version.");
        }
        int length = BinaryPrimitives.ReadInt32LittleEndian(state[12..]);
        if (length <= 0 || length != state.Length - HeaderSize)
        {
            throw new InvalidDataException("Invalid Cubase project state payload length.");
        }
        var payload = state[HeaderSize..].ToArray();
        CubaseAraMetadata? ara = null;
        if (version == 2)
        {
            var envelope = new CborReader(payload);
            if (envelope.ReadStartArray() != 2)
                throw new InvalidDataException("Invalid Cubase ARA state envelope.");
            var projectPayload = envelope.ReadByteString();
            ara = JsonSerializer.Deserialize<CubaseAraMetadata>(envelope.ReadByteString())
                ?? throw new InvalidDataException("Missing Cubase ARA association.");
            envelope.ReadEndArray();
            if (envelope.BytesRemaining != 0)
                throw new InvalidDataException("Cubase ARA state contains trailing data.");
            payload = projectPayload;
        }
        var reader = new CborReader(payload);
        reader.SkipValue();
        if (reader.BytesRemaining != 0)
        {
            throw new InvalidDataException("Cubase project state contains trailing payload data.");
        }
        using var stream = new MemoryStream(payload, writable: false);
        var project = new TuneLabProjectCbor().DeserializeNative(stream);
        ara?.Validate(project);
        return new(project, ara);
    }
}
