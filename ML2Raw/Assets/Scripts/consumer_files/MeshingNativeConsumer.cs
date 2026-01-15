using System;
using System.Collections.Generic;
using System.IO;
using MagicLeap.Android;
using UnityEngine;

/// <summary>
/// MeshingNativeConsumer captures spatial mesh data including:
/// - 3D mesh of the environment (vertices, triangles)
/// - Optional normals and confidence values
/// - Block-based updates (new, updated, deleted blocks)
/// 
/// File format:
/// - Header: "MESHING\0" (8 bytes) + version (4 bytes) + flags (4 bytes)
/// - Per update: timestamp, block count, vertex count, triangle count,
///               block infos, vertices, indices, normals, confidence
/// 
/// Permission: com.magicleap.permission.SPATIAL_MAPPING
/// </summary>
public class MeshingNativeConsumer : MonoBehaviour
{
    private const string SpatialMappingPermission = "com.magicleap.permission.SPATIAL_MAPPING";

    [Header("Mesh Settings")]
    [Tooltip("Compute vertex normals")]
    [SerializeField] private bool computeNormals = true;

    [Tooltip("Compute confidence values per vertex")]
    [SerializeField] private bool computeConfidence = false;

    [Tooltip("Level of detail: 0=Min, 1=Medium, 2=Max")]
    [SerializeField] private int levelOfDetail = 1;

    [Tooltip("Query region extent (meters)")]
    [SerializeField] private float queryExtent = 10f;

    [Tooltip("Update interval (seconds)")]
    [SerializeField] private float updateInterval = 1f;

    [Tooltip("Max blocks to request per update (to avoid timeouts)")]
    [SerializeField] private int maxBlocksPerRequest = 20;

    [Tooltip("Max hole perimeter to fill (meters, 0-5)")]
    [SerializeField] private float fillHoleLength = 0.5f;

    [Tooltip("Min disconnected area to keep (meters^2, 0-2)")]
    [SerializeField] private float disconnectedArea = 0.25f;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private bool isRunning = false;
    [SerializeField] private int totalBlocks = 0;
    [SerializeField] private int totalVertices = 0;
    [SerializeField] private int totalTriangles = 0;

    private bool started = false;
    private uint updateIndex = 0;
    private float lastUpdateTime = 0f;
    private float lastMeshRequestTime = 0f;

    private enum State { Idle, WaitingForInfo, WaitingForMesh }
    private State currentState = State.Idle;

    // Buffers for mesh data
    private float[] vertexBuffer;
    private ushort[] indexBuffer;
    private float[] normalBuffer;
    private float[] confidenceBuffer;
    private const int MAX_VERTICES = 500000;
    private const int MAX_INDICES = 1500000;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    void Start()
    {
        // Allocate buffers
        vertexBuffer = new float[MAX_VERTICES * 3];
        indexBuffer = new ushort[MAX_INDICES];
        if (computeNormals) normalBuffer = new float[MAX_VERTICES * 3];
        if (computeConfidence) confidenceBuffer = new float[MAX_VERTICES];

        Permissions.RequestPermission(SpatialMappingPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (perm != SpatialMappingPermission) return;
        if (started) return;
        started = true;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[MESH] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        // Build flags
        uint flags = 0;
        if (computeNormals) flags |= (uint)MLMeshingNative.MeshingFlags.ComputeNormals;
        if (computeConfidence) flags |= (uint)MLMeshingNative.MeshingFlags.ComputeConfidence;
        flags |= (uint)MLMeshingNative.MeshingFlags.RemoveMeshSkirt;

        bool ok = MLMeshingNative.MLMeshingUnity_Init(flags, fillHoleLength, disconnectedArea);
        Debug.Log($"[MESH] Init result: {ok} (flags={flags})");

        if (!ok)
        {
            Debug.LogError("[MESH] Init failed. Check com.magicleap.permission.SPATIAL_MAPPING.");
            enabled = false;
            return;
        }

        // Create output file
        outPath = Path.Combine(Application.persistentDataPath,
            $"meshing_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // Write header
        bw.Write(new byte[] { (byte)'M', (byte)'E', (byte)'S', (byte)'H', (byte)'I', (byte)'N', (byte)'G', 0 });
        bw.Write(1); // version
        bw.Write(flags);

        isRunning = true;
        Debug.Log("[MESH] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[MESH] Permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || !isRunning || bw == null) return;

        // Update query region to follow head position
        Vector3 headPos = Vector3.zero;
        
        // Try our native head tracking first
        MLHeadTrackingNative.HeadPoseData pose;
        if (MLHeadTrackingNative.MLHeadTrackingUnity_GetPose(out pose) && pose.resultCode == 0)
        {
            headPos = new Vector3(pose.position_x, pose.position_y, pose.position_z);
        }
        else if (Camera.main != null)
        {
            // Fallback to Camera.main
            headPos = Camera.main.transform.position;
        }
        
        MLMeshingNative.MLMeshingUnity_SetQueryRegion(
            headPos.x, headPos.y, headPos.z,
            queryExtent, queryExtent, queryExtent);
        
        if (debugMode && updateIndex == 0 && Time.frameCount % 300 == 0)
        {
            Debug.Log($"[MESH] Query region center: ({headPos.x:F2}, {headPos.y:F2}, {headPos.z:F2}) extent={queryExtent}m");
        }

        switch (currentState)
        {
            case State.Idle:
                if (Time.time - lastUpdateTime >= updateInterval)
                {
                    if (debugMode && updateIndex == 0)
                    {
                        Debug.Log($"[MESH] Requesting mesh info...");
                    }
                    MLMeshingNative.MLMeshingUnity_RequestMeshInfo();
                    currentState = State.WaitingForInfo;
                    lastUpdateTime = Time.time;
                }
                break;

            case State.WaitingForInfo:
                MLMeshingNative.MeshSummary summary;
                if (MLMeshingNative.MLMeshingUnity_GetMeshSummary(out summary))
                {
                    totalBlocks = summary.totalBlocks;

                    if (debugMode)
                    {
                        Debug.Log($"[MESH] Blocks: {summary.totalBlocks} " +
                                  $"(new={summary.newBlocks} updated={summary.updatedBlocks} deleted={summary.deletedBlocks})");
                    }

                    // Request mesh for new/updated blocks
                    if (summary.newBlocks > 0 || summary.updatedBlocks > 0)
                    {
                        List<int> blockIndices = new List<int>();
                        for (int i = 0; i < summary.totalBlocks; i++)
                        {
                            MLMeshingNative.MeshBlockInfo info;
                            if (MLMeshingNative.MLMeshingUnity_GetBlockInfo(i, out info))
                            {
                                if (info.State == MLMeshingNative.MeshBlockState.New ||
                                    info.State == MLMeshingNative.MeshBlockState.Updated)
                                {
                                    blockIndices.Add(i);
                                    
                                    // Limit blocks per request to avoid timeouts
                                    if (blockIndices.Count >= maxBlocksPerRequest)
                                    {
                                        break;
                                    }
                                }
                            }
                        }

                        if (blockIndices.Count > 0)
                        {
                            if (debugMode && blockIndices.Count < summary.newBlocks + summary.updatedBlocks)
                            {
                                Debug.Log($"[MESH] Requesting {blockIndices.Count} of {summary.newBlocks + summary.updatedBlocks} blocks (limited to {maxBlocksPerRequest})");
                            }
                            
                            MLMeshingNative.MLMeshingUnity_RequestMesh(
                                blockIndices.ToArray(), blockIndices.Count, levelOfDetail);
                            currentState = State.WaitingForMesh;
                            lastMeshRequestTime = Time.time;
                        }
                        else
                        {
                            currentState = State.Idle;
                        }
                    }
                    else
                    {
                        currentState = State.Idle;
                    }
                }
                // Timeout after 5 seconds
                else if (Time.time - lastUpdateTime > 5f)
                {
                    currentState = State.Idle;
                }
                break;

            case State.WaitingForMesh:
                // Poll for mesh result
                if (MLMeshingNative.MLMeshingUnity_PollMeshResult())
                {
                    // Result is ready (either success or failure)
                    int vertexCount, indexCount;
                    if (MLMeshingNative.MLMeshingUnity_IsMeshReady(out vertexCount, out indexCount))
                    {
                        if (vertexCount * 3 <= vertexBuffer.Length && indexCount <= indexBuffer.Length)
                        {
                            bool ok = MLMeshingNative.MLMeshingUnity_GetMeshData(
                                vertexBuffer, vertexBuffer.Length,
                                indexBuffer, indexBuffer.Length,
                                normalBuffer,
                                confidenceBuffer);

                            if (ok)
                            {
                                updateIndex++;
                                totalVertices = vertexCount;
                                totalTriangles = indexCount / 3;

                                WriteMeshUpdate(vertexCount, indexCount);

                                if (debugMode)
                                {
                                    Debug.Log($"[MESH] update={updateIndex} vertices={vertexCount} " +
                                              $"triangles={indexCount / 3}");
                                }
                            }
                        }
                        else
                        {
                            Debug.LogWarning($"[MESH] Buffer overflow: need {vertexCount * 3} verts, {indexCount} indices");
                        }
                    }
                    else
                    {
                        // Request completed but no mesh data (blocks may have failed)
                        if (debugMode)
                        {
                            Debug.LogWarning("[MESH] Mesh request completed but no valid data returned");
                        }
                    }
                    
                    currentState = State.Idle;
                }
                // Timeout after 10 seconds
                else if (Time.time - lastMeshRequestTime > 10f)
                {
                    Debug.LogWarning("[MESH] Mesh request timed out");
                    currentState = State.Idle;
                }
                break;
        }
    }

    private void WriteMeshUpdate(int vertexCount, int indexCount)
    {
        try
        {
            bw.Write(updateIndex);
            bw.Write(Time.time);
            bw.Write(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());

            bw.Write(vertexCount);
            bw.Write(indexCount);
            bw.Write(computeNormals ? 1 : 0);
            bw.Write(computeConfidence ? 1 : 0);

            // Write vertices (xyz float)
            for (int i = 0; i < vertexCount * 3; i++)
            {
                bw.Write(vertexBuffer[i]);
            }

            // Write indices (uint16)
            for (int i = 0; i < indexCount; i++)
            {
                bw.Write(indexBuffer[i]);
            }

            // Write normals if available
            if (computeNormals && normalBuffer != null)
            {
                for (int i = 0; i < vertexCount * 3; i++)
                {
                    bw.Write(normalBuffer[i]);
                }
            }

            // Write confidence if available
            if (computeConfidence && confidenceBuffer != null)
            {
                for (int i = 0; i < vertexCount; i++)
                {
                    bw.Write(confidenceBuffer[i]);
                }
            }

            bw.Flush();
            fs.Flush();
        }
        catch (Exception e)
        {
            Debug.LogError("[MESH] Write error: " + e);
        }
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (!started) return;
        started = false;
        isRunning = false;

        try { MLMeshingNative.MLMeshingUnity_Shutdown(); } catch { }

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
            Debug.Log($"[MESH] Saved {updateIndex} updates to: {outPath}");
        }
    }

    void OnDestroy() => Shutdown();
}