using System;
using System.IO;
using MagicLeap.Android;
using UnityEngine;

/// <summary>
/// CVCameraNativeConsumer captures camera pose data from Magic Leap 2 CV Camera API.
/// 
/// IMPORTANT: Requires HeadTrackingNativeConsumer to be initialized first.
/// Set Script Execution Order: 
///   - PerceptionManager = -100
///   - HeadTrackingNativeConsumer = -50
///   - CVCameraNativeConsumer = 0
/// 
/// File format:
/// - Header: "CVPOSE\0\0" (8 bytes) + version (4 bytes int)
/// - Records: Each record contains:
///   - frameIndex (uint32)
///   - unityTime (float)
///   - timestampNs (int64)
///   - resultCode (int32)
///   - rotation (4x float: x,y,z,w)
///   - position (3x float: x,y,z)
/// </summary>
public class CVCameraNativeConsumer : MonoBehaviour
{
    private const string CameraPermission = "android.permission.CAMERA";

    [Header("Settings")]
    [Tooltip("How often to sample pose (in seconds). 0 = every frame.")]
    [SerializeField] private float sampleInterval = 0f;

    [Tooltip("If true, prints extra logs.")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private uint frameCount = 0;

    private bool started = false;
    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint frameIndex = 0;
    private float lastSampleTime = 0f;

    void Start()
    {
        Permissions.RequestPermission(CameraPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (perm != CameraPermission) return;
        if (started) return;

        Debug.Log("[CVCAMERA] Camera permission granted");

        // Check perception
        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[CVCAMERA] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        // Check head tracking
        if (!HeadTrackingNativeConsumer.IsReady)
        {
            Debug.LogError("[CVCAMERA] HeadTrackingNativeConsumer not ready. " +
                           "Make sure it exists and has lower execution order.");
            enabled = false;
            return;
        }

        // Get head tracking handle
        ulong headHandle = HeadTrackingNativeConsumer.GetHandle();
        if (headHandle == 0)
        {
            Debug.LogError("[CVCAMERA] Failed to get head tracking handle.");
            enabled = false;
            return;
        }

        Debug.Log($"[CVCAMERA] Using head tracking handle: {headHandle}");

        // Initialize CV camera with head tracking handle
        bool ok = MLCVCameraNative.MLCVCameraUnity_Init(headHandle);
        if (!ok)
        {
            Debug.LogError("[CVCAMERA] MLCVCameraUnity_Init failed. Check logcat for details.");
            enabled = false;
            return;
        }

        // Setup output file
        outPath = Path.Combine(Application.persistentDataPath,
            $"cvpose_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // File header
        bw.Write(new byte[] { (byte)'C', (byte)'V', (byte)'P', (byte)'O', (byte)'S', (byte)'E', 0, 0 });
        bw.Write(1); // version

        started = true;
        isRunning = true;
        Debug.Log("[CVCAMERA] Initialized. Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[CVCAMERA] Camera permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Rate limiting
        if (sampleInterval > 0f)
        {
            if (Time.time - lastSampleTime < sampleInterval) return;
            lastSampleTime = Time.time;
        }

        // Get current pose
        MLCVCameraNative.CVCameraPose pose;
        bool gotPose = MLCVCameraNative.MLCVCameraUnity_GetPose(
            0, // 0 = use current time
            MLCVCameraNative.CVCameraID.ColorCamera,
            out pose
        );

        frameIndex++;
        frameCount = frameIndex;

        // Write record
        bw.Write(frameIndex);
        bw.Write(Time.time);
        bw.Write(pose.timestampNs);
        bw.Write(pose.resultCode);

        // Rotation
        bw.Write(pose.rotation_x);
        bw.Write(pose.rotation_y);
        bw.Write(pose.rotation_z);
        bw.Write(pose.rotation_w);

        // Position
        bw.Write(pose.position_x);
        bw.Write(pose.position_y);
        bw.Write(pose.position_z);

        // Debug logging
        if (debugMode && gotPose && (frameIndex % 60 == 0))
        {
            Debug.Log($"[CVCAMERA] frame={frameIndex} ts={pose.timestampNs} " +
                      $"pos=({pose.position_x:F3},{pose.position_y:F3},{pose.position_z:F3})");
        }

        // Periodic flush
        if (frameIndex % 30 == 0)
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
            isRunning = false;

            try { MLCVCameraNative.MLCVCameraUnity_Shutdown(); }
            catch (Exception e) { Debug.LogWarning("[CVCAMERA] Shutdown exception: " + e); }
        }

        CleanupFile();
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
            Debug.Log($"[CVCAMERA] Saved {frameIndex} frames to: {outPath}");
        }
    }
}