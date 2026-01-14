using System;
using System.IO;
using MagicLeap.Android;
using UnityEngine;

/// <summary>
/// GazeRecognitionNativeConsumer captures gaze behavior data including:
/// - Behavior classification (Fixation, Saccade, Pursuit, Blink, etc.)
/// - Eye-in-skull position
/// - Behavior timing (onset, duration)
/// - Movement metrics (velocity, amplitude, direction)
/// 
/// File format:
/// - Header: "GAZERECO" (8 bytes) + version (4 bytes) + sampleRate (4 bytes)
/// - Samples: timestamp, behavior, eye positions, metadata
/// 
/// Permission: com.magicleap.permission.EYE_TRACKING
/// </summary>
public class GazeRecognitionNativeConsumer : MonoBehaviour
{
    private const string EyeTrackingPermission = "com.magicleap.permission.EYE_TRACKING";

    [Header("Settings")]
    [Tooltip("Target sample rate")]
    [SerializeField] private int targetSampleRate = 60;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private uint sampleCount = 0;
    [SerializeField] private string currentBehavior = "Unknown";

    private bool started = false;
    private uint frameIndex = 0;
    private float lastSampleTime = 0f;
    private float sampleInterval;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    void Start()
    {
        sampleInterval = 1f / targetSampleRate;
        Permissions.RequestPermission(EyeTrackingPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (perm != EyeTrackingPermission) return;
        if (started) return;
        started = true;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[GAZE] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        bool ok = MLGazeRecognitionNative.MLGazeRecognitionUnity_Init();
        Debug.Log($"[GAZE] Init result: {ok}");

        if (!ok)
        {
            Debug.LogError("[GAZE] Init failed. Check com.magicleap.permission.EYE_TRACKING.");
            enabled = false;
            return;
        }

        // Create output file
        outPath = Path.Combine(Application.persistentDataPath,
            $"gazereco_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // Write header
        bw.Write(new byte[] { (byte)'G', (byte)'A', (byte)'Z', (byte)'E', (byte)'R', (byte)'E', (byte)'C', (byte)'O' });
        bw.Write(1); // version
        bw.Write(targetSampleRate);

        isRunning = true;
        Debug.Log("[GAZE] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[GAZE] Permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || !isRunning || bw == null) return;

        // Rate limiting
        if (Time.time - lastSampleTime < sampleInterval) return;
        lastSampleTime = Time.time;

        MLGazeRecognitionNative.GazeRecognitionData data;
        bool ok = MLGazeRecognitionNative.MLGazeRecognitionUnity_GetLatest(out data);

        if (!ok) return;

        frameIndex++;
        sampleCount = frameIndex;
        currentBehavior = data.BehaviorName;

        // Write sample
        WriteSample(data);

        if (debugMode && (frameIndex % 60 == 0))
        {
            string extra = "";
            if (data.Behavior == MLGazeRecognitionNative.GazeBehavior.Saccade ||
                data.Behavior == MLGazeRecognitionNative.GazeBehavior.Pursuit)
            {
                extra = $" vel={data.velocity_degps:F1}°/s amp={data.amplitude_deg:F1}° dir={data.direction_radial:F0}°";
            }

            Debug.Log($"[GAZE] frame={frameIndex} behavior={data.BehaviorName} " +
                      $"duration={data.duration_s:F2}s " +
                      $"eyeL=({data.eye_left_x:F2},{data.eye_left_y:F2}) " +
                      $"eyeR=({data.eye_right_x:F2},{data.eye_right_y:F2}){extra}");
        }

        if ((frameIndex % 60) == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private void WriteSample(MLGazeRecognitionNative.GazeRecognitionData data)
    {
        try
        {
            bw.Write(frameIndex);
            bw.Write(Time.time);
            bw.Write(data.timestampNs);

            // Behavior
            bw.Write(data.behavior);

            // Eye positions
            bw.Write(data.eye_left_x);
            bw.Write(data.eye_left_y);
            bw.Write(data.eye_right_x);
            bw.Write(data.eye_right_y);

            // Timing metadata
            bw.Write(data.onset_s);
            bw.Write(data.duration_s);

            // Movement metrics (NaN for non-movement behaviors)
            bw.Write(data.velocity_degps);
            bw.Write(data.amplitude_deg);
            bw.Write(data.direction_radial);

            // Error
            bw.Write(data.error);
        }
        catch (Exception e)
        {
            Debug.LogError("[GAZE] Write error: " + e);
        }
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (!started) return;
        started = false;
        isRunning = false;

        try { MLGazeRecognitionNative.MLGazeRecognitionUnity_Shutdown(); } catch { }

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
            Debug.Log($"[GAZE] Saved {frameIndex} samples to: {outPath}");
        }
    }

    void OnDestroy() => Shutdown();
}