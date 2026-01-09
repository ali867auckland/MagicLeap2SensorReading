using System;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class WorldCameraNativeConsumer : MonoBehaviour
{
    private const string CameraPermission = "android.permission.CAMERA";

    // MLWorldCameraIdentifier bitmask: Left=1, Right=2, Center=4
    [Header("WorldCam Config")]
    [SerializeField] private uint identifiersMask = 4; // Center

    private byte[] buf = new byte[8 * 1024 * 1024];
    private GCHandle handle;
    private IntPtr ptr;

    private bool started = false;
    private uint frameIndex = 0;

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
            Debug.LogError("[WORLD CAM] PerceptionManager not ready. Make sure PerceptionManager exists in the startup scene.");
            enabled = false;
            return;
        }

        started = true;

        bool ok = MLWorldCamNative.MLWorldCamUnity_Init(identifiersMask);
        Debug.Log("MLWorldCamUnity_Init: " + ok);
        if (!ok)
        {
            Debug.LogError("[WORLD CAM] Init failed.");
            enabled = false;
            return;
        }
    }


    private void OnDenied(string perm)
    {
        Debug.LogError("Camera permission denied: " + perm);
        enabled = false;
    }

    void Update()
    {
        if (!started) return;

        bool ok = MLWorldCamNative.MLWorldCamUnity_TryGetLatest(
            0,
            out var info,
            ptr,
            buf.Length,
            out int written);

        // IMPORTANT: native returns false when capacity is too small,
        // but still tells us the required size in `written`.
        if (!ok)
        {
            if (written > buf.Length)
            {
                ResizeBuffer(written);
            }
            return;
        }

        if (written <= 0) return;

        frameIndex++;

        if (frameIndex % 30 == 0)
        {
            Debug.Log($"[WORLD CAM] cam={info.camId} {info.width}x{info.height} stride={info.strideBytes} bpp={info.bytesPerPixel} type={info.frameType} ts={info.timestampNs} bytes={written}");
        }

        // buf now contains the frame bytes in the first `written` bytes.
    }

    private void ResizeBuffer(int neededBytes)
    {
        // Add a little headroom to avoid resizing repeatedly
        int newSize = neededBytes + (neededBytes / 8);

        if (handle.IsAllocated) handle.Free();
        buf = new byte[newSize];
        handle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        ptr = handle.AddrOfPinnedObject();

        Debug.Log($"[WORLD CAM] Resized buffer to {newSize} bytes (needed {neededBytes})");
    }


    void OnDestroy()
    {
        try { MLWorldCamNative.MLWorldCamUnity_Shutdown(); } catch { }

        if (handle.IsAllocated) handle.Free();
    }
}
