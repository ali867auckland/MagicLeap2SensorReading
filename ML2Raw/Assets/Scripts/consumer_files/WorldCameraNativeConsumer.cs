using System;
using System.IO;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class WorldCameraNativeConsumer : MonoBehaviour
{
    private const string CameraPermission = "android.permission.CAMERA";

    [Header("Camera Selection")]
    [Tooltip("Capture LEFT camera (1016x1016)")]
    [SerializeField] private bool captureLeft = true;
    
    [Tooltip("Capture RIGHT camera (1016x1016)")]
    [SerializeField] private bool captureRight = true;
    
    [Tooltip("Capture CENTER camera (1016x1016)")]
    [SerializeField] private bool captureCenter = true;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private uint leftFrames = 0;
    [SerializeField] private uint rightFrames = 0;
    [SerializeField] private uint centerFrames = 0;
    [SerializeField] private uint totalFrames = 0;

    // Separate buffers for each camera to avoid contention
    private byte[] bufLeft = new byte[2 * 1024 * 1024];
    private byte[] bufRight = new byte[2 * 1024 * 1024];
    private byte[] bufCenter = new byte[2 * 1024 * 1024];
    
    private GCHandle handleLeft, handleRight, handleCenter;
    private IntPtr ptrLeft, ptrRight, ptrCenter;

    private bool started = false;
    private uint frameIndex = 0;
    private float initTime = 0f;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    void Start()
    {
        // Allocate pinned buffers
        handleLeft = GCHandle.Alloc(bufLeft, GCHandleType.Pinned);
        ptrLeft = handleLeft.AddrOfPinnedObject();
        
        handleRight = GCHandle.Alloc(bufRight, GCHandleType.Pinned);
        ptrRight = handleRight.AddrOfPinnedObject();
        
        handleCenter = GCHandle.Alloc(bufCenter, GCHandleType.Pinned);
        ptrCenter = handleCenter.AddrOfPinnedObject();

        Permissions.RequestPermission(CameraPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (started) return;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[WORLD CAM] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        // Build camera mask from checkboxes
        uint cameraMask = 0;
        if (captureLeft) cameraMask |= MLWorldCamNative.CAM_LEFT;
        if (captureRight) cameraMask |= MLWorldCamNative.CAM_RIGHT;
        if (captureCenter) cameraMask |= MLWorldCamNative.CAM_CENTER;

        if (cameraMask == 0)
        {
            Debug.LogError("[WORLD CAM] No cameras selected!");
            enabled = false;
            return;
        }

        outPath = Path.Combine(Application.persistentDataPath,
            $"worldcam_raw_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // File header
        bw.Write(new byte[] { (byte)'W', (byte)'O', (byte)'R', (byte)'L', (byte)'D', (byte)'C', (byte)'A', (byte)'M' });
        bw.Write(2); // version 2 = multi-camera
        bw.Write(cameraMask);

        started = true;

        // Log which cameras are enabled
        string cams = "";
        if (captureLeft) cams += "LEFT ";
        if (captureRight) cams += "RIGHT ";
        if (captureCenter) cams += "CENTER ";
        Debug.Log($"[WORLD CAM] Mode: {cams.Trim()} (mask={cameraMask})");

        bool ok = MLWorldCamNative.MLWorldCamUnity_Init(cameraMask);
        Debug.Log("[WORLD CAM] Init result: " + ok);

        if (!ok)
        {
            Debug.LogError("[WORLD CAM] Init failed. Check android.permission.CAMERA.");
            CleanupFile();
            enabled = false;
            return;
        }

        initTime = Time.time;
        Debug.Log("[WORLD CAM] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[WORLD CAM] Permission denied: " + perm);
        enabled = false;
    }

    /// <summary>
    /// Compute image brightness statistics from grayscale buffer.
    /// </summary>
    private void ComputeBrightnessStats(byte[] buffer, int bytesWritten,
        out int minBrightness, out int maxBrightness, out float avgBrightness)
    {
        minBrightness = 255;
        maxBrightness = 0;
        avgBrightness = 0f;

        if (bytesWritten <= 0) return;

        long sum = 0;
        int step = 16;
        int sampleCount = 0;

        for (int i = 0; i < bytesWritten; i += step)
        {
            byte val = buffer[i];
            if (val < minBrightness) minBrightness = val;
            if (val > maxBrightness) maxBrightness = val;
            sum += val;
            sampleCount++;
        }

        if (sampleCount > 0)
        {
            avgBrightness = (float)sum / sampleCount;
        }
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Wait for camera to warm up
        if (Time.time - initTime < 2.0f) return;

        // Poll each enabled camera
        if (captureLeft) TryCapture(MLWorldCamNative.CAM_LEFT, bufLeft, ptrLeft, "LEFT", ref leftFrames);
        if (captureRight) TryCapture(MLWorldCamNative.CAM_RIGHT, bufRight, ptrRight, "RIGHT", ref rightFrames);
        if (captureCenter) TryCapture(MLWorldCamNative.CAM_CENTER, bufCenter, ptrCenter, "CENTER", ref centerFrames);

        // Periodic flush
        if ((totalFrames % 30) == 0 && totalFrames > 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private void TryCapture(uint camId, byte[] buffer, IntPtr ptr, string camName, ref uint camFrameCount)
    {
        // Check if this camera has a new frame
        if (!MLWorldCamNative.MLWorldCamUnity_HasNewFrame(camId)) return;

        bool ok = MLWorldCamNative.MLWorldCamUnity_TryGetLatest(
            camId, out var info, ptr, buffer.Length, out int written);

        if (!ok || written <= 0) return;

        frameIndex++;
        totalFrames = frameIndex;
        camFrameCount++;

        // Write frame with camera ID
        bw.Write(frameIndex);
        bw.Write(info.timestampNs);
        bw.Write(info.camId);
        bw.Write(info.frameType);
        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(written);
        bw.Write(buffer, 0, written);

        if (debugMode && (camFrameCount % 30 == 0))
        {
            ComputeBrightnessStats(buffer, written, out int minB, out int maxB, out float avgB);
            Debug.Log($"[WORLD CAM] {camName} frame={camFrameCount} {info.width}x{info.height} " +
                      $"brightness: min={minB} max={maxB} avg={avgB:F0}");
        }
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (!started) return;
        started = false;

        try { MLWorldCamNative.MLWorldCamUnity_Shutdown(); } catch { }
        CleanupFile();
        FreeBuffers();

        Debug.Log($"[WORLD CAM] Saved {totalFrames} frames (L={leftFrames} R={rightFrames} C={centerFrames}) to: {outPath}");
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

    private void FreeBuffers()
    {
        if (handleLeft.IsAllocated) handleLeft.Free();
        if (handleRight.IsAllocated) handleRight.Free();
        if (handleCenter.IsAllocated) handleCenter.Free();
        ptrLeft = ptrRight = ptrCenter = IntPtr.Zero;
    }

    void OnDestroy()
    {
        Shutdown();
    }
}