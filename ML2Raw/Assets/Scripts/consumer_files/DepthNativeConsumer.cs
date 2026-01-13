using System;
using System.IO;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class DepthNativeConsumer : MonoBehaviour
{
    private const string DepthPermission = "com.magicleap.permission.DEPTH_CAMERA";

    // Streams (native expects MLDepthCameraSettings.streams bitmask)
    private const uint STREAM_LONG  = 1u << 0;
    private const uint STREAM_SHORT = 1u << 1;

    // Flags - start minimal for reliability
    private const uint FLAG_DEPTH = 1u << 0;

    // Frame rate enum (native will pick safe value based on stream)
    private const uint FPS_5_ENUM = 1;

    [Header("Depth Stream Selection")]
    [Tooltip("Short = close range (0.2-0.9m), Long = far range (1-5m).")]
    [SerializeField] private bool useShortRange = true;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    private byte[] buf = new byte[8 * 1024 * 1024];
    private GCHandle bufHandle;
    private IntPtr bufPtr = IntPtr.Zero;

    private bool started = false;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint frameIndex = 0;

    void Start()
    {
        bufHandle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        bufPtr = bufHandle.AddrOfPinnedObject();

        Permissions.RequestPermission(DepthPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        Debug.Log("[DEPTH] Perception ready? " + PerceptionManager.IsReady);
        if (perm != DepthPermission) return;
        if (started) return;
        started = true;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[DEPTH] PerceptionManager not ready.");
            CleanupFile();
            enabled = false;
            return;
        }

        outPath = Path.Combine(Application.persistentDataPath,
            $"depth_raw_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // File header
        bw.Write(new byte[] { (byte)'D',(byte)'E',(byte)'P',(byte)'T',(byte)'H',(byte)'R',(byte)'A',(byte)'W' });
        bw.Write(1); // version

        uint streams = useShortRange ? STREAM_SHORT : STREAM_LONG;
        uint flags = FLAG_DEPTH;

        if (debugMode)
        {
            Debug.Log($"[DEPTH] Requesting: streams={streams} (short={useShortRange}) flags={flags}");
        }

        try { MLDepthNative.MLDepthUnity_Shutdown(); } catch { }

        bool ok = MLDepthNative.MLDepthUnity_Init(streams, flags, FPS_5_ENUM);
        Debug.Log($"[DEPTH] Init result: {ok}");

        if (!ok)
        {
            Debug.LogError("[DEPTH] Init failed. Check com.magicleap.permission.DEPTH_CAMERA.");
            CleanupFile();
            enabled = false;
            return;
        }

        Debug.Log("[DEPTH] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[DEPTH] Permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || bw == null) return;

        MLDepthNative.DepthFrameInfo info;
        int bytesWritten;

        bool got = MLDepthNative.MLDepthUnity_TryGetLatestDepth(
            0, out info, bufPtr, buf.Length, out bytesWritten);

        if (!got || bytesWritten <= 0) return;

        frameIndex++;

        if (debugMode && (frameIndex % 30 == 0))
        {
            Debug.Log($"[DEPTH] frame={frameIndex} {info.width}x{info.height} bytes={bytesWritten}");
        }

        // Write frame
        bw.Write(frameIndex);
        bw.Write(info.captureTimeNs);
        bw.Write((byte)1);
        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(bytesWritten);
        bw.Write(buf, 0, bytesWritten);

        if ((frameIndex % 30) == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (started)
        {
            started = false;
            try { MLDepthNative.MLDepthUnity_Shutdown(); } catch { }
        }
        CleanupFile();
        if (bufHandle.IsAllocated) bufHandle.Free();
        bufPtr = IntPtr.Zero;
    }

    private void CleanupFile()
    {
        try { bw?.Flush(); } catch { }
        try { fs?.Flush(); } catch { }
        try { bw?.Close(); } catch { }
        try { fs?.Close(); } catch { }
        bw = null;
        fs = null;
    }
}