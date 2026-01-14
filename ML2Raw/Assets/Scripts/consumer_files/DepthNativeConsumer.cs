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
    
    [Tooltip("Also capture long range (alternates between short and long).")]
    [SerializeField] private bool captureBothRanges = true;
    
    [Tooltip("Seconds between stream switches when capturing both.")]
    [SerializeField] private float streamSwitchInterval = 2f;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private string currentRange = "N/A";
    [SerializeField] private uint totalFrames = 0;
    [SerializeField] private uint shortFrames = 0;
    [SerializeField] private uint longFrames = 0;

    private byte[] buf = new byte[8 * 1024 * 1024];
    private GCHandle bufHandle;
    private IntPtr bufPtr = IntPtr.Zero;

    private bool started = false;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint frameIndex = 0;
    
    // Stream switching for dual-range capture
    private bool currentlyShort = true;
    private float lastSwitchTime = 0f;
    private uint shortFrameCount = 0;
    private uint longFrameCount = 0;

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
        bw.Write(2); // version (2 = supports both ranges)
        bw.Write(captureBothRanges ? 1 : 0); // both ranges flag

        // Start with short or long based on setting
        currentlyShort = useShortRange;
        uint streams = currentlyShort ? STREAM_SHORT : STREAM_LONG;
        uint flags = FLAG_DEPTH;

        // Clear mode logging
        if (captureBothRanges)
        {
            string startRange = currentlyShort ? "SHORT" : "LONG";
            Debug.Log($"[DEPTH] Mode: BOTH RANGES (alternating every {streamSwitchInterval}s, starting with {startRange})");
        }
        else
        {
            string rangeStr = useShortRange ? "SHORT (0.2-0.9m)" : "LONG (1-5m)";
            Debug.Log($"[DEPTH] Mode: {rangeStr} only");
        }

        if (debugMode)
        {
            Debug.Log($"[DEPTH] Requesting: streams={streams} flags={flags}");
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
        lastSwitchTime = Time.time;
        currentRange = currentlyShort ? "SHORT" : "LONG";
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[DEPTH] Permission denied: " + perm);
        enabled = false;
    }

    /// <summary>
    /// Compute depth statistics from the buffer.
    /// Depth data is float32 (4 bytes per pixel) in meters.
    /// </summary>
    private void ComputeDepthStats(int width, int height, int bytesPerPixel, int bytesWritten,
        out float minDepth, out float maxDepth, out float avgDepth, out int validCount)
    {
        minDepth = float.MaxValue;
        maxDepth = float.MinValue;
        avgDepth = 0f;
        validCount = 0;

        // Depth is typically float32 (4 bytes per pixel)
        if (bytesPerPixel != 4)
        {
            // Handle other formats if needed
            minDepth = maxDepth = avgDepth = 0f;
            return;
        }

        int pixelCount = bytesWritten / 4;
        double sum = 0;

        for (int i = 0; i < pixelCount && (i * 4 + 3) < bytesWritten; i++)
        {
            float depth = BitConverter.ToSingle(buf, i * 4);

            // Filter invalid values (0, NaN, Inf, negative)
            if (depth > 0.01f && depth < 100f && !float.IsNaN(depth) && !float.IsInfinity(depth))
            {
                if (depth < minDepth) minDepth = depth;
                if (depth > maxDepth) maxDepth = depth;
                sum += depth;
                validCount++;
            }
        }

        if (validCount > 0)
        {
            avgDepth = (float)(sum / validCount);
        }
        else
        {
            minDepth = maxDepth = avgDepth = 0f;
        }
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Check if we need to switch streams (for dual-range capture)
        if (captureBothRanges && (Time.time - lastSwitchTime >= streamSwitchInterval))
        {
            SwitchDepthStream();
            lastSwitchTime = Time.time;
        }

        MLDepthNative.DepthFrameInfo info;
        int bytesWritten;

        bool got = MLDepthNative.MLDepthUnity_TryGetLatestDepth(
            0, out info, bufPtr, buf.Length, out bytesWritten);

        if (!got || bytesWritten <= 0) return;

        frameIndex++;
        if (currentlyShort) shortFrameCount++; else longFrameCount++;

        // Update inspector status
        currentRange = currentlyShort ? "SHORT" : "LONG";
        totalFrames = frameIndex;
        shortFrames = shortFrameCount;
        longFrames = longFrameCount;

        if (debugMode && (frameIndex % 30 == 0))
        {
            // Compute actual depth statistics
            ComputeDepthStats(info.width, info.height, info.bytesPerPixel, bytesWritten,
                out float minD, out float maxD, out float avgD, out int validPx);

            float coverage = (float)validPx / (info.width * info.height) * 100f;
            string rangeStr = currentlyShort ? "SHORT" : "LONG";

            Debug.Log($"[DEPTH] frame={frameIndex} {rangeStr} {info.width}x{info.height} " +
                      $"min={minD:F2}m max={maxD:F2}m avg={avgD:F2}m " +
                      $"valid={validPx} ({coverage:F0}%)");
        }

        // Write frame with range indicator
        bw.Write(frameIndex);
        bw.Write(info.captureTimeNs);
        bw.Write((byte)(currentlyShort ? 1 : 2)); // 1=SHORT, 2=LONG
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

    private void SwitchDepthStream()
    {
        // Shutdown current stream
        try { MLDepthNative.MLDepthUnity_Shutdown(); } catch { }

        // Toggle stream
        currentlyShort = !currentlyShort;
        currentRange = currentlyShort ? "SHORT" : "LONG";
        uint streams = currentlyShort ? STREAM_SHORT : STREAM_LONG;
        uint flags = FLAG_DEPTH;

        string rangeStr = currentlyShort ? "SHORT (0.2-0.9m)" : "LONG (1-5m)";
        Debug.Log($"[DEPTH] >>> Switching to {rangeStr} <<<");

        // Reinitialize with new stream
        bool ok = MLDepthNative.MLDepthUnity_Init(streams, flags, FPS_5_ENUM);
        if (!ok)
        {
            Debug.LogError($"[DEPTH] Failed to switch to {rangeStr}");
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

        if (!string.IsNullOrEmpty(outPath) && frameIndex > 0)
        {
            if (captureBothRanges)
            {
                Debug.Log($"[DEPTH] Saved {frameIndex} frames (SHORT={shortFrameCount}, LONG={longFrameCount}) to: {outPath}");
            }
            else
            {
                Debug.Log($"[DEPTH] Saved {frameIndex} frames to: {outPath}");
            }
        }
    }
}