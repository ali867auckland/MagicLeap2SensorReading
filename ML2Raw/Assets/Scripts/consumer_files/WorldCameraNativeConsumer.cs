using System;
using System.IO;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class WorldCameraNativeConsumer : MonoBehaviour
{
    private const string CameraPermission = "android.permission.CAMERA";

    // MLWorldCameraIdentifier: Left=1, Right=2, Center=4
    private const uint CAM_CENTER = 4;

    [Header("WorldCam Config")]
    [Tooltip("Camera mask: 1=Left, 2=Right, 4=Center, 7=All. Center (4) is most reliable.")]
    [SerializeField] private uint identifiersMask = CAM_CENTER;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    private byte[] buf = new byte[8 * 1024 * 1024];
    private GCHandle handle;
    private IntPtr ptr;

    private bool started = false;
    private uint frameIndex = 0;
    private float initTime = 0f;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    void Start()
    {
        handle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        ptr = handle.AddrOfPinnedObject();

        Permissions.RequestPermission(CameraPermission, OnGranted, OnDenied, OnDenied);
    }

    private void OnGranted(string perm)
    {
        if (started) return;

        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[WORLD CAM] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        outPath = Path.Combine(Application.persistentDataPath,
            $"worldcam_raw_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // File header
        bw.Write(new byte[] { (byte)'W', (byte)'O', (byte)'R', (byte)'L', (byte)'D', (byte)'C', (byte)'A', (byte)'M' });
        bw.Write(1); // version
        bw.Write(identifiersMask);

        started = true;

        Debug.Log($"[WORLD CAM] Requesting cameras mask={identifiersMask}");

        bool ok = MLWorldCamNative.MLWorldCamUnity_Init(identifiersMask);
        Debug.Log("[WORLD CAM] Init result: " + ok);

        if (!ok)
        {
            Debug.LogError("[WORLD CAM] Init failed. Check android.permission.CAMERA.");
            CleanupFile();
            enabled = false;
            return;
        }

        initTime = Time.time;
        Debug.Log("[WORLD CAM] Output file: " + outPath);
    }

    private void OnDenied(string perm)
    {
        Debug.LogError("[WORLD CAM] Permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Wait for camera to warm up
        if (Time.time - initTime < 2.0f) return;

        bool ok = MLWorldCamNative.MLWorldCamUnity_TryGetLatest(
            100, out var info, ptr, buf.Length, out int written);

        if (!ok)
        {
            if (written > buf.Length) ResizeBuffer(written);
            return;
        }

        if (written <= 0) return;

        frameIndex++;

        // Write frame
        bw.Write(frameIndex);
        bw.Write(info.timestampNs);
        bw.Write(info.camId);
        bw.Write(info.frameType);
        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(written);
        bw.Write(buf, 0, written);

        if (debugMode && (frameIndex % 30 == 0))
        {
            Debug.Log($"[WORLD CAM] frame={frameIndex} cam={info.camId} {info.width}x{info.height} bytes={written}");
        }

        if ((frameIndex % 30) == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private void ResizeBuffer(int neededBytes)
    {
        int newSize = neededBytes + (neededBytes / 8);
        if (handle.IsAllocated) handle.Free();
        buf = new byte[newSize];
        handle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        ptr = handle.AddrOfPinnedObject();
        Debug.Log($"[WORLD CAM] Resized buffer to {newSize} bytes");
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (!started) return;
        started = false;

        try { MLWorldCamNative.MLWorldCamUnity_Shutdown(); } catch { }

        CleanupFile();

        if (handle.IsAllocated) handle.Free();
        ptr = IntPtr.Zero;
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

    void OnDestroy() => Shutdown();
}