using System;

public enum SensorType : ushort
{
    Depth = 1,
    WorldCamera = 2,
    Imu = 3,
}

public class MuxTcpSender
{
    private readonly SensorTcpLink link;
    private readonly byte[] magic = { (byte)'M', (byte)'L', (byte)'2', (byte)'S' };

    public MuxTcpSender(SensorTcpLink link)
    {
        this.link = link;
    }

    public bool SendFrame(
        SensorType sensorType,
        ushort streamId,
        uint frameIndex,
        long timestampNs,
        int width,
        int height,
        uint dtype,
        byte[] payload,
        int payloadLen)
    {
        if (link == null || !link.IsConnected) return false;

        // Header (40 bytes): <4s I H H I Q I I I I  (little-endian)
        Span<byte> header = stackalloc byte[40];
        int o = 0;

        Write(header, ref o, magic);                    // 4
        Write(header, ref o, (uint)1);                  // 4
        Write(header, ref o, (ushort)sensorType);       // 2
        Write(header, ref o, (ushort)streamId);         // 2
        Write(header, ref o, (uint)frameIndex);         // 4
        Write(header, ref o, (ulong)timestampNs);       // 8
        Write(header, ref o, (uint)width);              // 4
        Write(header, ref o, (uint)height);             // 4
        Write(header, ref o, (uint)dtype);              // 4
        Write(header, ref o, (uint)payloadLen);         // 4

        if (!link.Write(header)) return false;
        if (payloadLen > 0 && (payload == null || payload.Length < payloadLen)) return false;
        if (payloadLen > 0 && !link.Write(payload, 0, payloadLen)) return false;
        return true;
    }

    private static void Write(Span<byte> buf, ref int offset, ushort v)
    {
        BitConverter.TryWriteBytes(buf.Slice(offset, 2), v);
        offset += 2;
    }

    private static void Write(Span<byte> buf, ref int offset, uint v)
    {
        BitConverter.TryWriteBytes(buf.Slice(offset, 4), v);
        offset += 4;
    }

    private static void Write(Span<byte> buf, ref int offset, ulong v)
    {
        BitConverter.TryWriteBytes(buf.Slice(offset, 8), v);
        offset += 8;
    }

    private static void Write(Span<byte> buf, ref int offset, byte[] v)
    {
        v.CopyTo(buf.Slice(offset));
        offset += v.Length;
    }
}
