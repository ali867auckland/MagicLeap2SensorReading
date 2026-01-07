using System;
using System.Net.Sockets;

public class SensorTcpLink
{
    private TcpClient client;
    private NetworkStream stream;

    public bool IsConnected => stream != null && client != null && client.Connected;

    public bool Connect(string host, int port)
    {
        try
        {
            client = new TcpClient();
            client.NoDelay = true;
            client.Connect(host, port);
            stream = client.GetStream();
            return true;
        }
        catch (Exception e)
        {
            UnityEngine.Debug.LogError("TCP connect failed: " + e.Message);
            stream = null;
            client = null;
            return false;
        }
    }

    public void Close()
    {
        try { stream?.Close(); } catch { }
        try { client?.Close(); } catch { }
        stream = null;
        client = null;
    }

    public bool Write(Span<byte> bytes)
    {
        if (stream == null || !stream.CanWrite) return false;
        stream.Write(bytes);
        return true;
    }

    public bool Write(byte[] bytes, int offset, int count)
    {
        if (stream == null || !stream.CanWrite) return false;
        stream.Write(bytes, offset, count);
        return true;
    }
}
