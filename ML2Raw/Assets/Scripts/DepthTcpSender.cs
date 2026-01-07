using System;

public class DepthTcpSender
{
    private readonly SensorTcpLink link;

    public DepthTcpSender(SensorTcpLink link)
    {
        this.link = link;
    }

    public bool SendDepthFrame(
        uint frameIndex,
        long timestampNs,
        int width,
        int height,
        byte[] payload,
        int payloadLen)
    {
        if (link == null || !link.IsConnected) return false;

        // Header layout: <4sIIQIIII  => 36 bytes
        // magic, ver, frame_idx, ts_ns, w, h, dtype, payload_len
        Span<byte> header = stackalloc byte[36];
        int o = 0;

        Write(header, ref o, new byte[] { (byte)'D', (byte)'E', (byte)'P', (byte)'0' });
        Write(header, ref o, (uint)1);               // version
        Write(header, ref o, frameIndex);
        Write(header, ref o, (ulong)timestampNs);
        Write(header, ref o, (uint)width);
        Write(header, ref o, (uint)height);
        Write(header, ref o, (uint)1);               // dtype = float32
        Write(header, ref o, (uint)payloadLen);

        if (!link.Write(header)) return false;
        if (!link.Write(payload, 0, payloadLen)) return false;
        return true;
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
