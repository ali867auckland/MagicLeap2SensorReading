using System;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class WorldCameraNativeConsumer : MonoBehaviour
{
    private const string CameraPermission = "android.permission.CAMERA";

    [Header("TCP")]
    [SerializeField] private string host = "172.24.15.112";
    [SerializeField] private int port = 5001;
    [SerializeField] private bool sendFrames = true;

    // bitmask: Left=1, Right=2, Center=4 (from MLWorldCameraIdentifier in header)
    [SerializeField] private uint identifiersMask = 4; // Center by default

    private SensorTcpLink link;
    private MuxTcpSender mux;

    private byte[] buf = new byte[4 * 1024 * 1024];
    private GCHandle handle;

    private uint frameIndex = 0;

    void Start()
    {
        Permissions.RequestPermission(CameraPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        bool ok = MLWorldCamNative.MLWorldCamUnity_Init(identifiersMask);
        Debug.Log("MLWorldCamUnity_Init: " + ok);
        if (!ok) { enabled = false; return; }

        handle = GCHandle.Alloc(buf, GCHandleType.Pinned);

        link = new SensorTcpLink();
        bool connected = link.Connect(host, port);
        Debug.Log("TCP connected: " + connected);
        if (!connected) { enabled = false; return; }

        mux = new MuxTcpSender(link);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("Camera permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!handle.IsAllocated || mux == null) return;

        var ptr = handle.AddrOfPinnedObject();

        if (MLWorldCamNative.MLWorldCamUnity_TryGetLatest(
                0,
                out var info,
                ptr,
                buf.Length,
                out int written))
        {
            // If buffer too small, native returns false but sets bytesWritten = required
            if (written > buf.Length)
            {
                handle.Free();
                buf = new byte[written];
                handle = GCHandle.Alloc(buf, GCHandleType.Pinned);
                return;
            }

            if (sendFrames)
            {
                // dtype: we’ll store bytesPerPixel so laptop knows how to interpret
                mux.SendFrame(
                    SensorType.WorldCamera,
                    streamId: (ushort)Mathf.Max(0, info.camId),  // 0/1/2
                    frameIndex: frameIndex,
                    timestampNs: info.timestampNs,
                    width: info.width,
                    height: info.height,
                    dtype: (uint)info.bytesPerPixel,
                    payload: buf,
                    payloadLen: written
                );
                frameIndex++;
            }

            if (frameIndex % 30 == 0)
            {
                Debug.Log($"WorldCam cam={info.camId} {info.width}x{info.height} bpp={info.bytesPerPixel} ts={info.timestampNs} bytes={written}");
            }
        }
    }

    void OnDestroy()
    {
        try { MLWorldCamNative.MLWorldCamUnity_Shutdown(); } catch { }
        try { link?.Close(); } catch { }

        if (handle.IsAllocated) handle.Free();
    }
}
