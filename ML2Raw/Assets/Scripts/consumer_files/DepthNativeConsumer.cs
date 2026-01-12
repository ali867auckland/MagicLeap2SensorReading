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

    // Flags (native expects MLDepthCameraFlags bitmask)
    private const uint FLAG_DEPTH        = 1u << 0; // DepthImage
    private const uint FLAG_CONF         = 1u << 1; // Confidence
    private const uint FLAG_DFLAGS       = 1u << 2; // DepthFlags
    private const uint FLAG_AMBIENT_RAW  = 1u << 3; // AmbientRawDepthImage
    private const uint FLAG_RAW          = 1u << 4; // RawDepthImage

    // MLDepthCameraFrameRate enum values
    private const uint FPS_1_ENUM  = 0;
    private const uint FPS_5_ENUM  = 1;
    private const uint FPS_25_ENUM = 2;

    [Header("Depth Stream Selection")]
    [Tooltip("Short = close range, supports higher FPS. Long = farther range, low FPS only.")]
    [SerializeField] private bool useShortRange = true;

    [Header("Optional Debug")]
    [Tooltip("If true, prints extra logs and enforces strict validation to prevent native crashes.")]
    [SerializeField] private bool debugMode = true;

    [Tooltip("If true, hard-fail when BOTH streams are requested (recommended).")]
    [SerializeField] private bool rejectBothStreams = true;

    // Big pinned buffer for any of the blocks
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

    private static uint PickFpsEnum(uint streams)
    {
        // Long range supports only 1 or 5 -> choose 5
        if (streams == STREAM_LONG) return FPS_5_ENUM;

        // Short range supports 25 -> choose 25
        if (streams == STREAM_SHORT) return FPS_25_ENUM;

        // If someone sets both (not supported in practice), bias to short/25.
        if ((streams & STREAM_SHORT) != 0) return FPS_25_ENUM;

        // fallback
        return FPS_5_ENUM;
    }

    private void OnGranted(string perm)
    {
        Debug.Log("[DEPTH] Perception ready? " + PerceptionManager.IsReady);
        if (perm != DepthPermission) return;
        if (started) return;
        started = true;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[DEPTH] PerceptionManager not ready. Make sure PerceptionManager exists in the startup scene.");
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

        // Choose ONE stream (short OR long).
        uint streams = useShortRange ? STREAM_SHORT : STREAM_LONG;

        // Optional strict protection against "both streams"
        if (rejectBothStreams && streams == (STREAM_LONG | STREAM_SHORT))
        {
            Debug.LogError("[DEPTH] Both streams requested. Depth camera does not support running both simultaneously.");
            CleanupFile();
            enabled = false;
            return;
        }

        // Request everything you want (you can reduce if you hit init failure)
        uint desiredFlags = FLAG_DEPTH | FLAG_CONF | FLAG_RAW | FLAG_DFLAGS | FLAG_AMBIENT_RAW;

        // Pick safe fps for the chosen stream
        uint fpsEnum = PickFpsEnum(streams);

        if (debugMode)
        {
            Debug.Log($"[DEPTH] Requested: streams={streams} (short={useShortRange}) flags={desiredFlags} -> fpsEnum={fpsEnum}");
            Debug.Log($"[DEPTH] Flags breakdown: DEPTH={(desiredFlags & FLAG_DEPTH)!=0}, CONF={(desiredFlags & FLAG_CONF)!=0}, DFLAGS={(desiredFlags & FLAG_DFLAGS)!=0}, RAW={(desiredFlags & FLAG_RAW)!=0}, AMBIENT_RAW={(desiredFlags & FLAG_AMBIENT_RAW)!=0}");
        }

        // Clean init (in case something left it running)
        try { MLDepthNative.MLDepthUnity_Shutdown(); } catch { }

        bool ok = MLDepthNative.MLDepthUnity_Init(streams, desiredFlags, fpsEnum);
        Debug.Log($"[DEPTH] init ok={ok} streams={streams} flags={desiredFlags} fpsEnum={fpsEnum}");

        if (!ok)
        {
            Debug.LogError("[DEPTH] Init failed. Try reducing flags (start with DEPTH only) to confirm camera works, then add flags back.");
            CleanupFile();
            enabled = false;
            return;
        }

        Debug.Log("[DEPTH] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("Depth permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Anchor a frame on processed depth (frame header is based on this)
        if (!WriteProcessedDepthFrameHeaderAndBlock())
            return;

        // Optional blocks
        WriteOptionalBlock(2, MLDepthNative.MLDepthUnity_TryGetLatestConfidence);
        WriteOptionalBlock(3, MLDepthNative.MLDepthUnity_TryGetLatestDepthFlags);
        WriteOptionalBlock(4, MLDepthNative.MLDepthUnity_TryGetLatestRawDepth);
        WriteOptionalBlock(5, MLDepthNative.MLDepthUnity_TryGetLatestAmbientRawDepth);

        if ((frameIndex % 30) == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private bool WriteProcessedDepthFrameHeaderAndBlock()
    {
        MLDepthNative.DepthFrameInfo info;
        int bytesWritten;

        bool got = MLDepthNative.MLDepthUnity_TryGetLatestDepth(
            0,
            out info,
            bufPtr,
            buf.Length,
            out bytesWritten
        );

        if (!got || bytesWritten <= 0) return false;

        frameIndex++;

        if (debugMode && (Time.frameCount % 60 == 0))
        {
            Debug.Log($"[DEPTH] OK frame={frameIndex} bytes={bytesWritten} w={info.width} h={info.height} stride={info.strideBytes} bpp={info.bytesPerPixel} t={info.captureTimeNs}");
        }

        // Frame header
        bw.Write(frameIndex);
        bw.Write(info.captureTimeNs);

        // Block 1 = processed depth
        bw.Write((byte)1);
        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(bytesWritten);
        bw.Write(buf, 0, bytesWritten);

        return true;
    }

    private delegate bool Getter(out MLDepthNative.DepthFrameInfo info, IntPtr outBytes, int capacityBytes, out int bytesWritten);

    private void WriteOptionalBlock(byte blockType, Getter getter)
    {
        MLDepthNative.DepthFrameInfo info;
        int bytesWritten;

        bool got = getter(out info, bufPtr, buf.Length, out bytesWritten);

        bw.Write(blockType);

        if (!got || bytesWritten <= 0)
        {
            // mark empty
            bw.Write(0); // width
            bw.Write(0); // height
            bw.Write(0); // stride
            bw.Write(0); // bpp
            bw.Write(0); // nbytes
            return;
        }

        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(bytesWritten);
        bw.Write(buf, 0, bytesWritten);
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

        // Perception is owned by PerceptionManager.cs (do NOT shutdown perception here)

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
