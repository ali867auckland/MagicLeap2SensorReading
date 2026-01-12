using System;
using System.Collections;
using System.IO;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

/// <summary>
/// RGBCameraNativeConsumer captures RGB camera frames along with synchronized
/// camera pose from the CV Camera API.
/// 
/// DEPENDENCY: Requires CVCameraNativeConsumer to be running.
/// The CV Camera provides the frame poses via MLCVCameraUnity_GetPose().
/// 
/// This gives you: RGB image + exact 6DOF camera pose at capture time.
/// 
/// File format:
/// - Header: "RGBPOSE\0" (8 bytes) + version (4 bytes) + captureMode (4 bytes)
/// - Frames: frameIndex, unityTime, timestampNs, width, height, stride, format,
///           pose_valid, pose_result_code, rotation(x,y,z,w), position(x,y,z),
///           bytesWritten, imageData
/// 
/// Script Execution Order: RGBCameraNativeConsumer = 10 (after CVCamera at 0)
/// </summary>
public class RGBCameraNativeConsumer : MonoBehaviour
{
    private const string CameraPermission = "android.permission.CAMERA";

    [Header("Capture Settings")]
    [Tooltip("Capture mode: Preview (640x480), Video (1280x720), Image (1920x1080 JPEG)")]
    [SerializeField] private MLRGBCameraNative.RGBCaptureMode captureMode = MLRGBCameraNative.RGBCaptureMode.Video;

    [Header("Timing Settings")]
    [Tooltip("Max time to wait for dependencies (seconds).")]
    [SerializeField] private float dependencyTimeout = 15f;

    [Tooltip("Check interval for dependencies (seconds).")]
    [SerializeField] private float dependencyCheckInterval = 0.1f;

    [Tooltip("Delay after init before starting capture (seconds).")]
    [SerializeField] private float postInitDelay = 1.0f;

    [Header("Debug")]
    [Tooltip("Print extra debug logs.")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private bool isWaitingForDependencies = false;
    [SerializeField] private bool hasPermission = false;
    [SerializeField] private uint frameCount = 0;
    [SerializeField] private uint framesWithValidPose = 0;

    // Buffer for frame data
    private byte[] buf = new byte[4 * 1024 * 1024]; // 4MB initial
    private GCHandle bufHandle;
    private IntPtr bufPtr = IntPtr.Zero;

    private bool nativeInitialized = false;
    private uint frameIndex = 0;

    // File output
    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    // Track state
    private bool lastPerceptionState = false;
    private bool lastCVCameraState = false;

    /// <summary>
    /// Check if CV Camera is ready (has valid poses).
    /// </summary>
    private bool IsCVCameraReady()
    {
        // Check if CV camera native is initialized
        return MLCVCameraNative.MLCVCameraUnity_IsInitialized();
    }

    void Start()
    {
        bufHandle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        bufPtr = bufHandle.AddrOfPinnedObject();

        Permissions.RequestPermission(CameraPermission, OnPermissionGranted, OnPermissionDenied, OnPermissionDenied);
    }

    private void OnPermissionGranted(string perm)
    {
        if (perm != CameraPermission) return;
        if (hasPermission) return;

        hasPermission = true;
        Debug.Log("[RGBCAMERA] Camera permission granted");

        StartCoroutine(InitializeWhenReady());
    }

    private void OnPermissionDenied(string perm)
    {
        Debug.LogError("[RGBCAMERA] Camera permission denied: " + perm);
        CleanupBuffer();
        enabled = false;
    }

    private IEnumerator InitializeWhenReady()
    {
        isWaitingForDependencies = true;

        if (debugMode)
        {
            Debug.Log("[RGBCAMERA] Waiting for PerceptionManager and CVCamera...");
        }

        float elapsed = 0f;

        // Wait for PerceptionManager
        while (!PerceptionManager.IsReady && elapsed < dependencyTimeout)
        {
            yield return new WaitForSeconds(dependencyCheckInterval);
            elapsed += dependencyCheckInterval;
        }

        if (!PerceptionManager.IsReady)
        {
            isWaitingForDependencies = false;
            Debug.LogError($"[RGBCAMERA] PerceptionManager not ready after {dependencyTimeout}s");
            CleanupBuffer();
            yield break;
        }

        if (debugMode)
        {
            Debug.Log($"[RGBCAMERA] PerceptionManager ready after {elapsed:F2}s, waiting for CVCamera...");
        }

        // Wait for CV Camera (provides synchronized poses)
        while (!IsCVCameraReady() && elapsed < dependencyTimeout)
        {
            yield return new WaitForSeconds(dependencyCheckInterval);
            elapsed += dependencyCheckInterval;
        }

        isWaitingForDependencies = false;

        if (!IsCVCameraReady())
        {
            Debug.LogWarning($"[RGBCAMERA] CVCamera not ready after {dependencyTimeout}s. " +
                           "RGB capture will work but poses may not be available. " +
                           "Make sure CVCameraNativeConsumer is in the scene.");
        }
        else if (debugMode)
        {
            Debug.Log($"[RGBCAMERA] CVCamera ready after {elapsed:F2}s");
        }

        lastPerceptionState = true;
        lastCVCameraState = IsCVCameraReady();

        // Initialize RGB camera
        if (!InitializeNative())
        {
            CleanupBuffer();
            yield break;
        }

        if (!InitializeFile())
        {
            ShutdownNative();
            CleanupBuffer();
            yield break;
        }

        // Post-init delay
        yield return new WaitForSeconds(postInitDelay);

        // Start capture
        if (!MLRGBCameraNative.MLRGBCameraUnity_StartCapture())
        {
            Debug.LogError("[RGBCAMERA] Failed to start capture");
            ShutdownNative();
            CleanupFile();
            CleanupBuffer();
            yield break;
        }

        isRunning = true;
        Debug.Log("[RGBCAMERA] Capture started. Output file: " + outPath);
    }

    private bool InitializeNative()
    {
        if (nativeInitialized) return true;

        bool ok = MLRGBCameraNative.MLRGBCameraUnity_Init(captureMode);
        if (!ok)
        {
            Debug.LogError("[RGBCAMERA] MLRGBCameraUnity_Init failed");
            return false;
        }

        nativeInitialized = true;

        if (debugMode)
        {
            Debug.Log($"[RGBCAMERA] Native initialized (mode={captureMode})");
        }

        return true;
    }

    private void ShutdownNative()
    {
        if (!nativeInitialized) return;

        try
        {
            MLRGBCameraNative.MLRGBCameraUnity_Shutdown();
        }
        catch (Exception e)
        {
            Debug.LogWarning("[RGBCAMERA] Native shutdown exception: " + e);
        }

        nativeInitialized = false;
    }

    private bool InitializeFile()
    {
        try
        {
            outPath = Path.Combine(Application.persistentDataPath,
                $"rgbpose_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

            fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
            bw = new BinaryWriter(fs);

            // Header: "RGBPOSE\0" + version + captureMode
            bw.Write(new byte[] { (byte)'R', (byte)'G', (byte)'B', (byte)'P', (byte)'O', (byte)'S', (byte)'E', 0 });
            bw.Write(1); // version
            bw.Write((int)captureMode);

            return true;
        }
        catch (Exception e)
        {
            Debug.LogError("[RGBCAMERA] Failed to create output file: " + e);
            return false;
        }
    }

    void Update()
    {
        // Check dependency states
        bool currentPerceptionState = PerceptionManager.IsReady;
        bool currentCVCameraState = IsCVCameraReady();

        if (lastPerceptionState && !currentPerceptionState)
        {
            Debug.LogWarning("[RGBCAMERA] Perception dropped, pausing...");
            isRunning = false;
            MLRGBCameraNative.MLRGBCameraUnity_StopCapture();
        }
        else if (!lastPerceptionState && currentPerceptionState)
        {
            Debug.Log("[RGBCAMERA] Perception recovered, re-initializing...");
            StartCoroutine(ReinitializeAfterRecovery());
        }

        // Log CV camera state changes
        if (debugMode && lastCVCameraState != currentCVCameraState)
        {
            if (currentCVCameraState)
                Debug.Log("[RGBCAMERA] CVCamera became available - poses will now be captured");
            else
                Debug.LogWarning("[RGBCAMERA] CVCamera became unavailable - poses will be invalid");
        }

        lastPerceptionState = currentPerceptionState;
        lastCVCameraState = currentCVCameraState;

        // Normal update
        if (!isRunning || !nativeInitialized || bw == null) return;

        // Try to get frame
        MLRGBCameraNative.RGBFrameWithPose info;
        int written;

        bool ok = MLRGBCameraNative.MLRGBCameraUnity_TryGetLatestFrame(
            100, // 100ms timeout
            out info,
            bufPtr,
            buf.Length,
            out written
        );

        // Handle buffer resize
        if (!ok && written > buf.Length)
        {
            ResizeBuffer(written);
            return;
        }

        if (!ok || written <= 0) return;

        frameIndex++;
        frameCount = frameIndex;
        if (info.HasValidPose) framesWithValidPose++;

        // Write frame
        WriteFrame(info, written);

        if (debugMode && (frameIndex % 30 == 0))
        {
            float poseRate = frameIndex > 0 ? (float)framesWithValidPose / frameIndex * 100f : 0f;
            Debug.Log($"[RGBCAMERA] frame={frameIndex} {info.width}x{info.height} " +
                      $"pose={info.HasValidPose} (r={info.pose_result_code}) " +
                      $"pos=({info.pose_position_x:F2},{info.pose_position_y:F2},{info.pose_position_z:F2}) " +
                      $"poseRate={poseRate:F1}%");
        }

        // Periodic flush
        if (frameIndex % 30 == 0)
        {
            try { bw.Flush(); fs.Flush(); }
            catch (Exception e) { Debug.LogError("[RGBCAMERA] Flush error: " + e); }
        }
    }

    private IEnumerator ReinitializeAfterRecovery()
    {
        yield return new WaitForSeconds(0.5f);

        if (!PerceptionManager.IsReady)
        {
            yield return new WaitForSeconds(1f);
        }

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[RGBCAMERA] Perception not ready, giving up re-init");
            yield break;
        }

        ShutdownNative();

        if (InitializeNative())
        {
            yield return new WaitForSeconds(postInitDelay);

            if (MLRGBCameraNative.MLRGBCameraUnity_StartCapture())
            {
                isRunning = true;
                Debug.Log("[RGBCAMERA] Re-initialized after recovery");
            }
            else
            {
                Debug.LogError("[RGBCAMERA] Failed to restart capture after recovery");
            }
        }
    }

    private void WriteFrame(MLRGBCameraNative.RGBFrameWithPose info, int bytesWritten)
    {
        try
        {
            bw.Write(frameIndex);
            bw.Write(Time.time);
            bw.Write(info.timestampNs);
            
            bw.Write(info.width);
            bw.Write(info.height);
            bw.Write(info.strideBytes);
            bw.Write(info.format);
            
            bw.Write(info.pose_valid);
            bw.Write(info.pose_result_code);
            
            bw.Write(info.pose_rotation_x);
            bw.Write(info.pose_rotation_y);
            bw.Write(info.pose_rotation_z);
            bw.Write(info.pose_rotation_w);
            
            bw.Write(info.pose_position_x);
            bw.Write(info.pose_position_y);
            bw.Write(info.pose_position_z);
            
            bw.Write(bytesWritten);
            bw.Write(buf, 0, bytesWritten);
        }
        catch (Exception e)
        {
            Debug.LogError("[RGBCAMERA] Write error: " + e);
        }
    }

    private void ResizeBuffer(int neededBytes)
    {
        int newSize = neededBytes + (neededBytes / 8);

        if (bufHandle.IsAllocated) bufHandle.Free();
        buf = new byte[newSize];
        bufHandle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        bufPtr = bufHandle.AddrOfPinnedObject();

        Debug.Log($"[RGBCAMERA] Resized buffer to {newSize} bytes");
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        isRunning = false;

        ShutdownNative();
        CleanupFile();
        CleanupBuffer();
    }

    private void CleanupFile()
    {
        try { bw?.Flush(); } catch { }
        try { fs?.Flush(); } catch { }
        try { bw?.Close(); } catch { }
        try { fs?.Close(); } catch { }
        bw = null;
        fs = null;

        if (!string.IsNullOrEmpty(outPath) && File.Exists(outPath))
        {
            float poseRate = frameIndex > 0 ? (float)framesWithValidPose / frameIndex * 100f : 0f;
            Debug.Log($"[RGBCAMERA] Saved {frameIndex} frames ({framesWithValidPose} with pose, {poseRate:F1}%) to: {outPath}");
        }
    }

    private void CleanupBuffer()
    {
        if (bufHandle.IsAllocated) bufHandle.Free();
        bufPtr = IntPtr.Zero;
    }

    void OnDestroy() => Shutdown();
}