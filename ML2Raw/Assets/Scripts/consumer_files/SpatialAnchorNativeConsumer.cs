using System;
using System.Collections.Generic;
using System.IO;
using MagicLeap.Android;
using UnityEngine;

public class SpatialAnchorNativeConsumer : MonoBehaviour
{
    private const string SpatialAnchorPermission = "com.magicleap.permission.SPATIAL_ANCHOR";

    [Header("Auto-Create Settings")]
    [SerializeField] private bool autoCreateEnabled = true;
    [SerializeField] private float minAnchorDistance = 0.5f;
    [SerializeField] private uint maxAnchors = 100;

    [Header("Sampling")]
    [SerializeField] private float sampleInterval = 0.1f;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private int anchorCount = 0;
    [SerializeField] private uint sampleCount = 0;
    [SerializeField] private float distanceToNearest = -1f;

    private bool started = false;
    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint sampleIndex = 0;
    private float lastSampleTime = 0f;
    private float lastUpdateTime = 0f;
    private const float UPDATE_INTERVAL = 0.1f;

    private List<(ulong, ulong)> trackedAnchorIds = new List<(ulong, ulong)>();

    void Start()
    {
        Permissions.RequestPermission(SpatialAnchorPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (perm != SpatialAnchorPermission) return;
        if (started) return;
        started = true;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[ANCHOR] PerceptionManager not ready");
            enabled = false;
            return;
        }

        bool ok = MLSpatialAnchorNative.MLSpatialAnchorUnity_Init();
        if (!ok)
        {
            Debug.LogError("[ANCHOR] Init failed");
            enabled = false;
            return;
        }

        MLSpatialAnchorNative.MLSpatialAnchorUnity_SetAutoCreate(
            autoCreateEnabled, 
            minAnchorDistance, 
            maxAnchors
        );

        outPath = Path.Combine(Application.persistentDataPath,
            $"anchors_dense_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        bw.Write(new byte[] { (byte)'M', (byte)'L', (byte)'A', (byte)'N', (byte)'C', (byte)'H', (byte)'R', (byte)'2' });
        bw.Write(2);
        bw.Write(autoCreateEnabled);
        bw.Write(minAnchorDistance);
        bw.Write(maxAnchors);

        isRunning = true;
        Debug.Log($"[ANCHOR] Initialized. Auto-create: {autoCreateEnabled}, MinDist: {minAnchorDistance}m, MaxAnchors: {maxAnchors}");
        Debug.Log($"[ANCHOR] Output file: {outPath}");
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[ANCHOR] Permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || bw == null) return;

        if (autoCreateEnabled && Time.time - lastUpdateTime >= UPDATE_INTERVAL)
        {
            lastUpdateTime = Time.time;
            MLSpatialAnchorNative.MLSpatialAnchorUnity_Update();

            int currentCount = MLSpatialAnchorNative.MLSpatialAnchorUnity_GetAnchorCount();
            if (currentCount > anchorCount)
            {
                RefreshAnchorList();
            }
            anchorCount = currentCount;
        }

        if (Time.time - lastSampleTime >= sampleInterval)
        {
            lastSampleTime = Time.time;
            SampleAllAnchors();
        }

        Vector3 headPos = Camera.main.transform.position;
        distanceToNearest = MLSpatialAnchorNative.MLSpatialAnchorUnity_GetDistanceToNearestAnchor(
            headPos.x, headPos.y, headPos.z
        );
    }

    private void RefreshAnchorList()
    {
        int maxCount = (int)maxAnchors;
        MLSpatialAnchorNative.AnchorPoseData[] poses = new MLSpatialAnchorNative.AnchorPoseData[maxCount];
        int count;

        if (MLSpatialAnchorNative.MLSpatialAnchorUnity_GetAllAnchors(poses, maxCount, out count))
        {
            trackedAnchorIds.Clear();
            for (int i = 0; i < count; i++)
            {
                trackedAnchorIds.Add((poses[i].anchorId_data0, poses[i].anchorId_data1));
            }

            if (debugMode)
            {
                Debug.Log($"[ANCHOR] Refreshed anchor list: {count} anchors");
            }
        }
    }

    private void SampleAllAnchors()
    {
        if (trackedAnchorIds.Count == 0) return;

        foreach (var (id0, id1) in trackedAnchorIds)
        {
            MLSpatialAnchorNative.AnchorPoseData pose;
            if (MLSpatialAnchorNative.MLSpatialAnchorUnity_GetAnchorPose(id0, id1, out pose))
            {
                WritePoseSample(ref pose);
                sampleIndex++;
                sampleCount = sampleIndex;
            }
        }

        if (debugMode && (sampleIndex % 300 == 0) && sampleIndex > 0)
        {
            Debug.Log($"[ANCHOR] Collected {sampleIndex} pose samples from {anchorCount} anchors " +
                      $"(nearest: {distanceToNearest:F2}m)");
        }

        if (sampleIndex % 100 == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private void WritePoseSample(ref MLSpatialAnchorNative.AnchorPoseData pose)
    {
        bw.Write(sampleIndex);
        bw.Write(Time.time);
        bw.Write(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());

        bw.Write(pose.anchorId_data0);
        bw.Write(pose.anchorId_data1);

        bw.Write(pose.rotation_x);
        bw.Write(pose.rotation_y);
        bw.Write(pose.rotation_z);
        bw.Write(pose.rotation_w);
        bw.Write(pose.position_x);
        bw.Write(pose.position_y);
        bw.Write(pose.position_z);

        bw.Write(pose.quality);
        bw.Write(pose.resultCode);
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (started)
        {
            started = false;
            isRunning = false;

            try { MLSpatialAnchorNative.MLSpatialAnchorUnity_Shutdown(); }
            catch (Exception e) { Debug.LogWarning("[ANCHOR] Shutdown exception: " + e); }
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
            Debug.Log($"[ANCHOR] Saved {sampleIndex} samples from {anchorCount} anchors to: {outPath}");
        }
    }
}