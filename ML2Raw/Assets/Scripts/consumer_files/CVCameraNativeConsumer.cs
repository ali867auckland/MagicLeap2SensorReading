using System;
using System.IO;
using MagicLeap.Android;
using UnityEngine;
using System.Threading;

/// <summary>
/// CVCameraNativeConsumer captures camera pose data from Magic Leap 2 CV Camera API.
///
/// Key fix:
/// - Do NOT poll CV pose with timestamp=0 every frame and treat failures as "broken".
/// - Instead, query pose using the latest RGB frame timestamp (MLTime) so it's aligned
///   to real camera frames and stays within the CV pose cache window.
///
/// Requires:
/// - PerceptionManager initialized
/// - HeadTrackingNativeConsumer initialized
/// - RGB pipeline publishing timestamps into SensorTimestampBus (2-line patch shown below)
///
/// File format (v2):
/// - Header: "CVPOSE\0\0" (8 bytes) + version (int32)
/// - Records:
///   - recordIndex (uint32)
///   - unityTime   (float)
///   - rgbFrameIndex (uint32)   // helps join with RGB file easily
///   - timestampNs (int64)      // MLTime for the RGB frame
///   - resultCode (int32)
///   - rotation (x,y,z,w float)
///   - position (x,y,z float)
/// </summary>
public class CVCameraNativeConsumer : MonoBehaviour
{
    private const string CameraPermission = "android.permission.CAMERA";

    [Header("Settings")]
    [Tooltip("How often to sample pose (in seconds). 0 = whenever a new RGB timestamp arrives.")]
    [SerializeField] private float sampleInterval = 0f;

    [Tooltip("Write failed records too (PoseNotFound etc). Usually keep OFF for clean datasets.")]
    [SerializeField] private bool writeFailedRecords = false;

    [Tooltip("If true, prints extra logs.")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private uint recordCount = 0;

    private bool started = false;
    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint recordIndex = 0;
    private float lastSampleTime = 0f;
    private long lastTimestampNs = 0;

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
        bw.Write(2); // version = 2

        started = true;
        isRunning = true;
        Debug.Log("[CVCAMERA] Initialized. Output file: " + outPath);
        Debug.Log("[CVCAMERA] Waiting for RGB timestamps (SensorTimestampBus.LatestRgbTimestampNs)...");
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[CVCAMERA] Camera permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || bw == null) return;

        // If user requested rate limiting (sampleInterval), enforce it.
        if (sampleInterval > 0f)
        {
            if (Time.time - lastSampleTime < sampleInterval) return;
            lastSampleTime = Time.time;
        }

        // Use latest RGB timestamp (best practice: CV pose queried at a frame timestamp)
        long ts = SensorTimestampBus.LatestRgbTimestampNs;
        uint rgbFrameIndex = SensorTimestampBus.LatestRgbFrameIndex;

        // If RGB isn't running yet, do nothing (avoid timestamp=0 polling spam)
        if (ts <= 0) return;

        // Only query when a new RGB timestamp arrives (prevents duplicate records)
        if (ts == lastTimestampNs) return;
        lastTimestampNs = ts;

        // Query pose for THIS frame timestamp
        MLCVCameraNative.CVCameraPose pose;
        bool gotPose = MLCVCameraNative.MLCVCameraUnity_GetPose(
            ts,
            MLCVCameraNative.CVCameraID.ColorCamera,
            out pose
        );

        // If failed, either skip (recommended) or optionally write failures for debugging
        bool ok = gotPose && pose.resultCode == 0;
        if (!ok && !writeFailedRecords)
        {
            if (debugMode)
            {
                Debug.LogWarning($"[CVCAMERA] Pose not available for ts={ts} r={pose.resultCode} (skipping)");
            }
            return;
        }

        // Write record
        recordIndex++;
        recordCount = recordIndex;

        bw.Write(recordIndex);
        bw.Write(Time.time);
        bw.Write(rgbFrameIndex);
        bw.Write(ts); // should match ts
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

        if (debugMode && (recordIndex % 60 == 0))
        {
            Debug.Log($"[CVCAMERA] rec={recordIndex} rgbFrame={rgbFrameIndex} ts={pose.timestampNs} r={pose.resultCode} " +
                      $"pos=({pose.position_x:F3},{pose.position_y:F3},{pose.position_z:F3})");
        }

        // Periodic flush
        if (recordIndex % 30 == 0)
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
            Debug.Log($"[CVCAMERA] Saved {recordIndex} records to: {outPath}");
        }
    }
}

/// <summary>
/// Shared bus for cross-script timestamps (simple + robust).
/// RGBCameraNativeConsumer should update these each time it receives a frame.
/// </summary>
public static class SensorTimestampBus
{
    private static long _latestRgbTimestampNs;
    private static int _latestRgbFrameIndex;

    // MUST be properties, not fields
    public static long LatestRgbTimestampNs => Interlocked.Read(ref _latestRgbTimestampNs);

    public static uint LatestRgbFrameIndex =>
        (uint)Interlocked.CompareExchange(ref _latestRgbFrameIndex, 0, 0);

    public static void Publish(long tsNs, uint frameIndex)
    {
        Interlocked.Exchange(ref _latestRgbTimestampNs, tsNs);
        Interlocked.Exchange(ref _latestRgbFrameIndex, (int)frameIndex);
    }
}

