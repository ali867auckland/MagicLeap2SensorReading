using System;
using System.IO;
using MagicLeap.Android;
using UnityEngine;

/// <parameter name="summary">
/// SpaceNativeConsumer captures Magic Leap Space localization data.
/// 
/// Tracks:
/// - Localization status (NotLocalized, Localized, Pending, etc.)
/// - Localization confidence (Poor, Fair, Good, Excellent)
/// - Space information (ID, name, type)
/// - Error flags (OutOfMappedArea, LowFeatureCount, etc.)
/// 
/// File format (version 1):
/// - Header: "MLSPACE\0" (8 bytes) + version (4 bytes int)
/// - Records: Each record contains:
///   - recordIndex (uint32)
///   - unityTime (float)
///   - timestampNs (int64)
///   - resultCode (int32)
///   - status (uint32)
///   - confidence (uint32)
///   - errorFlags (uint32)
///   - spaceType (uint32)
///   - spaceId_data0 (uint64)
///   - spaceId_data1 (uint64)
///   - spaceName (64 bytes string)
///   - targetSpaceOrigin (16 bytes)
/// </summary>
public class SpaceNativeConsumer : MonoBehaviour
{
    private const string SpacePermission = "com.magicleap.permission.SPACE_MANAGER";

    [Header("Settings")]
    [Tooltip("How often to poll localization status (in seconds).")]
    [SerializeField] private float sampleInterval = 1f;

    [Tooltip("If true, prints extra logs.")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private uint recordCount = 0;
    [SerializeField] private string currentStatus = "Unknown";
    [SerializeField] private string currentConfidence = "Unknown";
    [SerializeField] private string currentSpaceName = "";
    [SerializeField] private string currentSpaceType = "";

    private bool started = false;
    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint recordIndex = 0;
    private float lastSampleTime = 0f;

    void Start()
    {
        Permissions.RequestPermission(SpacePermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (perm != SpacePermission) return;
        if (started) return;
        started = true;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[SPACE] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        // Initialize Space manager
        bool ok = MLSpaceNative.MLSpaceUnity_Init();
        if (!ok)
        {
            Debug.LogError("[SPACE] MLSpaceUnity_Init failed. Check logcat for details.");
            enabled = false;
            return;
        }

        // Setup output file
        outPath = Path.Combine(Application.persistentDataPath,
            $"space_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // File header: "MLSPACE\0" + version 1
        bw.Write(new byte[] { (byte)'M', (byte)'L', (byte)'S', (byte)'P', (byte)'A', (byte)'C', (byte)'E', 0 });
        bw.Write(1); // version

        isRunning = true;
        Debug.Log("[SPACE] Initialized. Output file: " + outPath);

        // Get available spaces
        ListAvailableSpaces();
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[SPACE] Permission denied: " + perm);
        enabled = false;
    }

    private void ListAvailableSpaces()
    {
        MLSpaceNative.SpaceInfo[] spaces = new MLSpaceNative.SpaceInfo[20];
        int count;

        if (MLSpaceNative.MLSpaceUnity_GetSpaceList(spaces, 20, out count))
        {
            Debug.Log($"[SPACE] Found {count} available spaces:");
            for (int i = 0; i < count; i++)
            {
                Debug.Log($"[SPACE]   {i+1}. {spaces[i].spaceName} (Type: {spaces[i].Type})");
            }
        }
        else
        {
            Debug.LogWarning("[SPACE] Could not retrieve space list");
        }
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Rate limiting
        if (Time.time - lastSampleTime < sampleInterval) return;
        lastSampleTime = Time.time;

        // Get current localization status
        MLSpaceNative.SpaceLocalizationData data;
        if (!MLSpaceNative.MLSpaceUnity_GetLocalizationStatus(out data))
        {
            return; // Failed to get status
        }

        recordIndex++;
        recordCount = recordIndex;

        // Update inspector status
        currentStatus = data.Status.ToString();
        currentConfidence = data.Confidence.ToString();
        currentSpaceName = data.spaceName ?? "";
        currentSpaceType = data.Type.ToString();

        // Write record
        bw.Write(recordIndex);
        bw.Write(Time.time);
        bw.Write(data.timestampNs);
        bw.Write(data.resultCode);

        // Localization state
        bw.Write(data.status);
        bw.Write(data.confidence);
        bw.Write(data.errorFlags);
        bw.Write(data.spaceType);

        // Space identification
        bw.Write(data.spaceId_data0);
        bw.Write(data.spaceId_data1);

        // Space name (fixed 64 bytes)
        byte[] nameBytes = new byte[64];
        if (!string.IsNullOrEmpty(data.spaceName))
        {
            byte[] encoded = System.Text.Encoding.UTF8.GetBytes(data.spaceName);
            int len = Math.Min(encoded.Length, 63);
            Array.Copy(encoded, nameBytes, len);
        }
        bw.Write(nameBytes);

        // Target space origin (16 bytes)
        if (data.targetSpaceOrigin != null && data.targetSpaceOrigin.Length >= 16)
        {
            bw.Write(data.targetSpaceOrigin, 0, 16);
        }
        else
        {
            bw.Write(new byte[16]);
        }

        // Debug logging
        if (debugMode && (recordIndex % 30 == 0))
        {
            string errStr = FormatErrorFlags(data.ErrorFlags);
            Debug.Log($"[SPACE] rec={recordIndex} status={data.Status} conf={data.Confidence} " +
                      $"space={data.spaceName} type={data.Type} err={errStr}");
        }

        // Periodic flush
        if (recordIndex % 10 == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private static string FormatErrorFlags(MLSpaceNative.SpaceLocalizationErrorFlag flags)
    {
        if (flags == MLSpaceNative.SpaceLocalizationErrorFlag.None) return "None";
        
        var parts = new System.Collections.Generic.List<string>();
        if ((flags & MLSpaceNative.SpaceLocalizationErrorFlag.Unknown) != 0) parts.Add("Unknown");
        if ((flags & MLSpaceNative.SpaceLocalizationErrorFlag.OutOfMappedArea) != 0) parts.Add("OutOfMappedArea");
        if ((flags & MLSpaceNative.SpaceLocalizationErrorFlag.LowFeatureCount) != 0) parts.Add("LowFeatureCount");
        if ((flags & MLSpaceNative.SpaceLocalizationErrorFlag.ExcessiveMotion) != 0) parts.Add("ExcessiveMotion");
        if ((flags & MLSpaceNative.SpaceLocalizationErrorFlag.LowLight) != 0) parts.Add("LowLight");
        if ((flags & MLSpaceNative.SpaceLocalizationErrorFlag.HeadposeFailure) != 0) parts.Add("HeadposeFailure");
        if ((flags & MLSpaceNative.SpaceLocalizationErrorFlag.AlgorithmFailure) != 0) parts.Add("AlgorithmFailure");
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

            try { MLSpaceNative.MLSpaceUnity_Shutdown(); }
            catch (Exception e) { Debug.LogWarning("[SPACE] Shutdown exception: " + e); }
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
            Debug.Log($"[SPACE] Saved {recordIndex} records to: {outPath}");
        }
    }
}