using System;
using MagicLeap.Android;
using UnityEngine;
using System.Runtime.InteropServices;

public class DepthNativeConsumer : MonoBehaviour
{
    private const string DepthPermission = "com.magicleap.permission.DEPTH_CAMERA";

    private const uint STREAM_LONG  = 1u << 0;
    private const uint STREAM_SHORT = 1u << 1;

    private const uint FLAG_DEPTH  = 1u << 0;
    private const uint FLAG_CONF   = 1u << 1;
    private const uint FLAG_DFLAGS = 1u << 2;

    private const uint FPS_5 = 1;

    private byte[] depthBuf = new byte[4 * 1024 * 1024];
    private GCHandle depthHandle;

    [Header("TCP")]
    [SerializeField] private string host = "172.24.15.112"; // your laptop IP on same WiFi
    [SerializeField] private int port = 5001;
    [SerializeField] private bool sendFrames = true;

    private SensorTcpLink link;
    private MuxTcpSender mux;

    private uint frameIndex = 0;

    void Start()
    {
        Permissions.RequestPermission(DepthPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        uint streams = STREAM_LONG;
        uint flags   = FLAG_DEPTH;
        uint fps     = FPS_5;

        bool ok = MLDepthNative.MLDepthUnity_Init(streams, flags, fps);
        Debug.Log("MLDepthUnity_Init: " + ok);
        if (!ok) { enabled = false; return; }

        // Allocate pinned buffer for depth payload
        depthHandle = GCHandle.Alloc(depthBuf, GCHandleType.Pinned);

        // Connect TCP
        link = new SensorTcpLink();
        bool connected = link.Connect(host, port);
        Debug.Log("TCP connected: " + connected);
        if (!connected) { enabled = false; return; }

        // Create mux sender AFTER TCP is connected
        mux = new MuxTcpSender(link);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("Depth permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!depthHandle.IsAllocated) return;
        if (mux == null) return;

        var ptr = depthHandle.AddrOfPinnedObject();

        if (MLDepthNative.MLDepthUnity_TryGetLatestDepth(0, out var info, ptr, depthBuf.Length, out int written))
        {
            if (written > depthBuf.Length)
            {
                depthHandle.Free();
                depthBuf = new byte[written];
                depthHandle = GCHandle.Alloc(depthBuf, GCHandleType.Pinned);
                return;
            }

            if (sendFrames)
            {
                mux.SendFrame(
                    SensorType.Depth,
                    streamId: 0,
                    frameIndex: frameIndex,
                    timestampNs: info.captureTimeNs,
                    width: info.width,
                    height: info.height,
                    dtype: 1, // float32
                    payload: depthBuf,
                    payloadLen: written
                );

                frameIndex++;
            }

            float firstDepth = BitConverter.ToSingle(depthBuf, 0);
            Debug.Log($"Depth frame {info.width}x{info.height} stride={info.strideBytes} time={info.captureTimeNs} firstDepth={firstDepth}");
        }
    }

    void OnDestroy()
    {
        try { MLDepthNative.MLDepthUnity_Shutdown(); } catch { }
        try { link?.Close(); } catch { }

        if (depthHandle.IsAllocated) depthHandle.Free();
    }
}
