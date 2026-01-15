using System;
using System.IO;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class EyeCameraNativeConsumer : MonoBehaviour
{
    private const string EyeCameraPermission = "com.magicleap.permission.EYE_CAMERA";

    [Header("Camera Selection")]
    [SerializeField] private bool captureLeftTemple = true;
    [SerializeField] private bool captureLeftNasal = true;
    [SerializeField] private bool captureRightNasal = true;
    [SerializeField] private bool captureRightTemple = true;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    [Header("Status (Read Only)")]
    [SerializeField] private uint leftTempleFrames = 0;
    [SerializeField] private uint leftNasalFrames = 0;
    [SerializeField] private uint rightNasalFrames = 0;
    [SerializeField] private uint rightTempleFrames = 0;
    [SerializeField] private uint totalFrames = 0;

    private byte[] bufLeftTemple = new byte[1 * 1024 * 1024];
    private byte[] bufLeftNasal = new byte[1 * 1024 * 1024];
    private byte[] bufRightNasal = new byte[1 * 1024 * 1024];
    private byte[] bufRightTemple = new byte[1 * 1024 * 1024];
    
    private GCHandle handleLT, handleLN, handleRN, handleRT;
    private IntPtr ptrLT, ptrLN, ptrRN, ptrRT;

    private bool started = false;
    private uint frameIndex = 0;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    void Start()
    {
        handleLT = GCHandle.Alloc(bufLeftTemple, GCHandleType.Pinned);
        ptrLT = handleLT.AddrOfPinnedObject();
        
        handleLN = GCHandle.Alloc(bufLeftNasal, GCHandleType.Pinned);
        ptrLN = handleLN.AddrOfPinnedObject();
        
        handleRN = GCHandle.Alloc(bufRightNasal, GCHandleType.Pinned);
        ptrRN = handleRN.AddrOfPinnedObject();
        
        handleRT = GCHandle.Alloc(bufRightTemple, GCHandleType.Pinned);
        ptrRT = handleRT.AddrOfPinnedObject();

        Permissions.RequestPermission(EyeCameraPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (perm != EyeCameraPermission) return;
        if (started) return;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[EYECAM] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        uint cameraMask = 0;
        if (captureLeftTemple) cameraMask |= MLEyeCameraNative.CAM_LEFT_TEMPLE;
        if (captureLeftNasal) cameraMask |= MLEyeCameraNative.CAM_LEFT_NASAL;
        if (captureRightNasal) cameraMask |= MLEyeCameraNative.CAM_RIGHT_NASAL;
        if (captureRightTemple) cameraMask |= MLEyeCameraNative.CAM_RIGHT_TEMPLE;

        if (cameraMask == 0)
        {
            Debug.LogError("[EYECAM] No cameras selected!");
            enabled = false;
            return;
        }

        outPath = Path.Combine(Application.persistentDataPath,
            $"eyecam_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        bw.Write(new byte[] { (byte)'E', (byte)'Y', (byte)'E', (byte)'C', (byte)'A', (byte)'M', 0, 0 });
        bw.Write(1);
        bw.Write(cameraMask);

        string cams = "";
        if (captureLeftTemple) cams += "LeftTemple ";
        if (captureLeftNasal) cams += "LeftNasal ";
        if (captureRightNasal) cams += "RightNasal ";
        if (captureRightTemple) cams += "RightTemple ";
        Debug.Log($"[EYECAM] Cameras: {cams.Trim()} (mask={cameraMask})");

        bool ok = MLEyeCameraNative.MLEyeCameraUnity_Init(cameraMask);
        Debug.Log($"[EYECAM] Init result: {ok}");

        if (!ok)
        {
            Debug.LogError("[EYECAM] Init failed. Check com.magicleap.permission.EYE_CAMERA.");
            CleanupFile();
            enabled = false;
            return;
        }

        started = true;
        Debug.Log("[EYECAM] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[EYECAM] Permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || bw == null) return;

        if (captureLeftTemple) TryCapture(MLEyeCameraNative.CAM_LEFT_TEMPLE, bufLeftTemple, ptrLT, "LeftTemple", ref leftTempleFrames);
        if (captureLeftNasal) TryCapture(MLEyeCameraNative.CAM_LEFT_NASAL, bufLeftNasal, ptrLN, "LeftNasal", ref leftNasalFrames);
        if (captureRightNasal) TryCapture(MLEyeCameraNative.CAM_RIGHT_NASAL, bufRightNasal, ptrRN, "RightNasal", ref rightNasalFrames);
        if (captureRightTemple) TryCapture(MLEyeCameraNative.CAM_RIGHT_TEMPLE, bufRightTemple, ptrRT, "RightTemple", ref rightTempleFrames);

        if ((totalFrames % 30) == 0 && totalFrames > 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private void TryCapture(uint camId, byte[] buffer, IntPtr ptr, string camName, ref uint camFrameCount)
    {
        if (!MLEyeCameraNative.MLEyeCameraUnity_HasNewFrame(camId)) return;

        MLEyeCameraNative.EyeCameraFrameInfo info;
        int written;

        bool ok = MLEyeCameraNative.MLEyeCameraUnity_TryGetLatestFrame(
            camId, 10, out info, ptr, buffer.Length, out written);

        if (!ok || written <= 0) return;

        frameIndex++;
        totalFrames = frameIndex;
        camFrameCount++;

        bw.Write(frameIndex);
        bw.Write(info.camera_id);
        bw.Write(info.frame_number);
        bw.Write(info.timestamp_ns);
        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.stride);
        bw.Write(info.bytes_per_pixel);
        bw.Write(written);
        bw.Write(buffer, 0, written);

        if (debugMode && (camFrameCount % 30 == 0))
        {
            Debug.Log($"[EYECAM] {camName} frame={camFrameCount} {info.width}x{info.height}");
        }
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (!started) return;
        started = false;

        try { MLEyeCameraNative.MLEyeCameraUnity_Shutdown(); } catch { }
        CleanupFile();
        FreeBuffers();

        Debug.Log($"[EYECAM] Saved {totalFrames} frames " +
                  $"(LT={leftTempleFrames} LN={leftNasalFrames} RN={rightNasalFrames} RT={rightTempleFrames}) " +
                  $"to: {outPath}");
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
        if (handleLT.IsAllocated) handleLT.Free();
        if (handleLN.IsAllocated) handleLN.Free();
        if (handleRN.IsAllocated) handleRN.Free();
        if (handleRT.IsAllocated) handleRT.Free();
        ptrLT = ptrLN = ptrRN = ptrRT = IntPtr.Zero;
    }

    void OnDestroy() => Shutdown();
}