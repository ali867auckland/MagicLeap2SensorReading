using System;
using System.IO;
using UnityEngine;

/// <summary>
/// HeadTrackingNativeConsumer captures head tracking (6DOF) data from Magic Leap 2
/// and saves it to a binary file.
/// 
/// IMPORTANT: This must initialize BEFORE CVCameraNativeConsumer.
/// Set Script Execution Order: HeadTrackingNativeConsumer = -50
/// 
/// File format (version 2):
/// - Header: "HEADPOSE" (8 bytes) + version (4 bytes int)
/// - Records: Each record contains:
///   - frameIndex (uint32)
///   - unityTime (float)
///   - timestampNs (int64)
///   - resultCode (int32)
///   - rotation (4x float: x,y,z,w)
///   - position (3x float: x,y,z)
///   - status (uint32) - HeadTrackingStatus enum
///   - confidence (float)
///   - errorFlags (uint32) - bitmask of HeadTrackingErrorFlag
///   - hasMapEvent (byte)
///   - mapEventsMask (uint64) - bitmask of HeadTrackingMapEvent
/// </summary>
public class HeadTrackingNativeConsumer : MonoBehaviour
{
    [Header("Settings")]
    [Tooltip("How often to sample pose (in seconds). 0 = every frame.")]
    [SerializeField] private float sampleInterval = 0f;

    [Tooltip("If true, prints extra logs.")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private uint frameCount = 0;
    [SerializeField] private float currentConfidence = 0f;
    [SerializeField] private string currentStatus = "";

    private static HeadTrackingNativeConsumer _instance;

    private bool started = false;
    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint frameIndex = 0;
    private float lastSampleTime = 0f;

    /// <summary>
    /// Returns true if head tracking is initialized and ready.
    /// CV Camera should check this before initializing.
    /// </summary>
    public static bool IsReady => _instance != null && _instance.started;

    /// <summary>
    /// Get the head tracking handle for CV Camera to use.
    /// </summary>
    public static ulong GetHandle()
    {
        if (_instance == null || !_instance.started) return 0;
        return MLHeadTrackingNative.MLHeadTrackingUnity_GetHandle();
    }

    void Awake()
    {
        if (_instance != null && _instance != this)
        {
            Debug.LogWarning("[HEADTRACKING] Duplicate instance destroyed");
            Destroy(gameObject);
            return;
        }
        _instance = this;
    }

    void Start()
    {
        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[HEADTRACKING] PerceptionManager not ready. Make sure PerceptionManager exists and runs first.");
            enabled = false;
            return;
        }

        // Initialize native head tracking
        bool ok = MLHeadTrackingNative.MLHeadTrackingUnity_Init();
        if (!ok)
        {
            Debug.LogError("[HEADTRACKING] MLHeadTrackingUnity_Init failed. Check logcat for details.");
            enabled = false;
            return;
        }

        // Setup output file
        outPath = Path.Combine(Application.persistentDataPath,
            $"headpose_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // File header: "HEADPOSE" + version 2 (updated format)
        bw.Write(new byte[] { (byte)'H', (byte)'E', (byte)'A', (byte)'D', (byte)'P', (byte)'O', (byte)'S', (byte)'E' });
        bw.Write(2); // version 2 - new format with status/errorFlags/mapEventsMask

        started = true;
        isRunning = true;
        Debug.Log("[HEADTRACKING] Initialized. Output file: " + outPath);
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

        // Get current head pose
        MLHeadTrackingNative.HeadPoseData pose;
        bool gotPose = MLHeadTrackingNative.MLHeadTrackingUnity_GetPose(out pose);

        frameIndex++;
        frameCount = frameIndex;
        currentConfidence = pose.confidence;
        currentStatus = pose.Status.ToString();

        // Write record
        bw.Write(frameIndex);
        bw.Write(Time.time);
        bw.Write(pose.timestampNs);
        bw.Write(pose.resultCode);

        // Rotation (quaternion)
        bw.Write(pose.rotation_x);
        bw.Write(pose.rotation_y);
        bw.Write(pose.rotation_z);
        bw.Write(pose.rotation_w);

        // Position
        bw.Write(pose.position_x);
        bw.Write(pose.position_y);
        bw.Write(pose.position_z);

        // Tracking state (new format)
        bw.Write(pose.status);      // uint32 HeadTrackingStatus
        bw.Write(pose.confidence);  // float
        bw.Write(pose.errorFlags);  // uint32 bitmask

        // Map events (bitmask format)
        bw.Write((byte)(pose.hasMapEvent ? 1 : 0));
        bw.Write(pose.mapEventsMask);  // uint64 bitmask

        // Debug logging
        if (debugMode && gotPose && (frameIndex % 60 == 0))
        {
            string errStr = FormatErrorFlags((MLHeadTrackingNative.HeadTrackingErrorFlag)pose.errorFlags);
            Debug.Log($"[HEADTRACKING] frame={frameIndex} ts={pose.timestampNs} " +
                      $"pos=({pose.position_x:F3},{pose.position_y:F3},{pose.position_z:F3}) " +
                      $"conf={pose.confidence:F2} status={pose.Status} err={errStr}");
        }

        // Log map events
        if (debugMode && pose.hasMapEvent)
        {
            string evtStr = FormatMapEvents((MLHeadTrackingNative.HeadTrackingMapEvent)pose.mapEventsMask);
            Debug.Log($"[HEADTRACKING] MAP EVENTS: {evtStr}");
        }

        // Periodic flush
        if (frameIndex % 30 == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private static string FormatErrorFlags(MLHeadTrackingNative.HeadTrackingErrorFlag flags)
    {
        if (flags == MLHeadTrackingNative.HeadTrackingErrorFlag.None) return "None";
        
        var parts = new System.Collections.Generic.List<string>();
        if ((flags & MLHeadTrackingNative.HeadTrackingErrorFlag.Unknown) != 0) parts.Add("Unknown");
        if ((flags & MLHeadTrackingNative.HeadTrackingErrorFlag.NotEnoughFeatures) != 0) parts.Add("NotEnoughFeatures");
        if ((flags & MLHeadTrackingNative.HeadTrackingErrorFlag.LowLight) != 0) parts.Add("LowLight");
        if ((flags & MLHeadTrackingNative.HeadTrackingErrorFlag.ExcessiveMotion) != 0) parts.Add("ExcessiveMotion");
        return string.Join("|", parts);
    }

    private static string FormatMapEvents(MLHeadTrackingNative.HeadTrackingMapEvent events)
    {
        if (events == MLHeadTrackingNative.HeadTrackingMapEvent.None) return "None";
        
        var parts = new System.Collections.Generic.List<string>();
        if ((events & MLHeadTrackingNative.HeadTrackingMapEvent.Lost) != 0) parts.Add("Lost");
        if ((events & MLHeadTrackingNative.HeadTrackingMapEvent.Recovered) != 0) parts.Add("Recovered");
        if ((events & MLHeadTrackingNative.HeadTrackingMapEvent.RecoveryFailed) != 0) parts.Add("RecoveryFailed");
        if ((events & MLHeadTrackingNative.HeadTrackingMapEvent.NewSession) != 0) parts.Add("NewSession");
        return string.Join("|", parts);
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (started)
        {
            started = false;
            isRunning = false;

            try { MLHeadTrackingNative.MLHeadTrackingUnity_Shutdown(); }
            catch (Exception e) { Debug.LogWarning("[HEADTRACKING] Shutdown exception: " + e); }
        }

        CleanupFile();

        if (_instance == this)
        {
            _instance = null;
        }
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
            Debug.Log($"[HEADTRACKING] Saved {frameIndex} frames to: {outPath}");
        }
    }
}