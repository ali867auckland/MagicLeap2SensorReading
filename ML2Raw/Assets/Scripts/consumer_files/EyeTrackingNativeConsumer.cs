using System;
using System.IO;
using MagicLeap.Android;
using UnityEngine;

/// <summary>
/// EyeTrackingNativeConsumer captures eye tracking data including:
/// - Gaze direction (left/right eye)
/// - Eye openness (0-1)
/// - Blink detection
/// - Vergence point (where eyes converge)
/// - Confidence values
/// 
/// File format:
/// - Header: "EYETRACK" (8 bytes) + version (4 bytes) + sampleRate (4 bytes)
/// - Samples: timestamp, confidence, blink, openness, gaze, vergence, positions
/// 
/// Permission: com.magicleap.permission.EYE_TRACKING
/// </summary>
public class EyeTrackingNativeConsumer : MonoBehaviour
{
    private const string EyeTrackingPermission = "com.magicleap.permission.EYE_TRACKING";

    [Header("Settings")]
    [Tooltip("Target sample rate (actual rate depends on system)")]
    [SerializeField] private int targetSampleRate = 60;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private uint sampleCount = 0;

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
            Debug.LogError("[EYE] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        bool ok = MLEyeTrackingNative.MLEyeTrackingUnity_Init();
        Debug.Log($"[EYE] Init result: {ok}");

        if (!ok)
        {
            Debug.LogError("[EYE] Init failed. Check com.magicleap.permission.EYE_TRACKING.");
            enabled = false;
            return;
        }

        // Create output file
        outPath = Path.Combine(Application.persistentDataPath,
            $"eyetrack_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // Write header
        bw.Write(new byte[] { (byte)'E', (byte)'Y', (byte)'E', (byte)'T', (byte)'R', (byte)'A', (byte)'C', (byte)'K' });
        bw.Write(1); // version
        bw.Write(targetSampleRate);

        isRunning = true;
        Debug.Log("[EYE] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[EYE] Permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || !isRunning || bw == null) return;

        // Rate limiting
        if (Time.time - lastSampleTime < sampleInterval) return;
        lastSampleTime = Time.time;

        MLEyeTrackingNative.EyeTrackingData data;
        bool ok = MLEyeTrackingNative.MLEyeTrackingUnity_GetLatest(out data);

        if (!ok) return;

        frameIndex++;
        sampleCount = frameIndex;

        // Write sample
        WriteSample(data);

        if (debugMode && (frameIndex % 60 == 0))
        {
            string blinkStr = "";
            if (data.LeftBlinking && data.RightBlinking) blinkStr = "BOTH";
            else if (data.LeftBlinking) blinkStr = "LEFT";
            else if (data.RightBlinking) blinkStr = "RIGHT";
            else blinkStr = "none";

            Debug.Log($"[EYE] frame={frameIndex} " +
                      $"openL={data.left_eye_openness:F2} openR={data.right_eye_openness:F2} " +
                      $"blink={blinkStr} " +
                      $"confL={data.left_center_confidence:F2} confR={data.right_center_confidence:F2} " +
                      $"gazeL=({data.left_gaze_x:F2},{data.left_gaze_y:F2},{data.left_gaze_z:F2})");
        }

        if ((frameIndex % 60) == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private void WriteSample(MLEyeTrackingNative.EyeTrackingData data)
    {
        try
        {
            bw.Write(frameIndex);
            bw.Write(Time.time);
            bw.Write(data.timestampNs);

            // Confidence
            bw.Write(data.vergence_confidence);
            bw.Write(data.left_center_confidence);
            bw.Write(data.right_center_confidence);

            // Blink state
            bw.Write((byte)(data.left_blink != 0 ? 1 : 0));
            bw.Write((byte)(data.right_blink != 0 ? 1 : 0));

            // Eye openness
            bw.Write(data.left_eye_openness);
            bw.Write(data.right_eye_openness);

            // Left gaze direction
            bw.Write(data.left_gaze_x);
            bw.Write(data.left_gaze_y);
            bw.Write(data.left_gaze_z);

            // Right gaze direction
            bw.Write(data.right_gaze_x);
            bw.Write(data.right_gaze_y);
            bw.Write(data.right_gaze_z);

            // Vergence point
            bw.Write(data.vergence_x);
            bw.Write(data.vergence_y);
            bw.Write(data.vergence_z);

            // Eye positions
            bw.Write(data.left_pos_x);
            bw.Write(data.left_pos_y);
            bw.Write(data.left_pos_z);
            bw.Write(data.right_pos_x);
            bw.Write(data.right_pos_y);
            bw.Write(data.right_pos_z);

            // Validity flags
            bw.Write((byte)(data.vergence_valid != 0 ? 1 : 0));
            bw.Write((byte)(data.left_valid != 0 ? 1 : 0));
            bw.Write((byte)(data.right_valid != 0 ? 1 : 0));

            // Error
            bw.Write(data.error);
        }
        catch (Exception e)
        {
            Debug.LogError("[EYE] Write error: " + e);
        }
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (!started) return;
        started = false;
        isRunning = false;

        try { MLEyeTrackingNative.MLEyeTrackingUnity_Shutdown(); } catch { }

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
            Debug.Log($"[EYE] Saved {frameIndex} samples to: {outPath}");
        }
    }

    void OnDestroy() => Shutdown();
}