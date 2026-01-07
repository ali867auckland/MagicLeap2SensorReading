using System;
using System.IO;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class DepthNativeConsumer : MonoBehaviour
{
    private const string DepthPermission = "com.magicleap.permission.DEPTH_CAMERA";

    private const uint STREAM_LONG  = 1u << 0;
    private const uint FLAG_DEPTH   = 1u << 0;
    private const uint FPS_5        = 1;

    private byte[] depthBuf = new byte[8 * 1024 * 1024];
    private GCHandle depthHandle;
    private IntPtr depthPtr = IntPtr.Zero;

    private bool started = false;
    private bool permissionGranted = false;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint frameIndex = 0;

    void Start()
    {
        // Pin buffer once (so native can write into it)
        depthHandle = GCHandle.Alloc(depthBuf, GCHandleType.Pinned);
        depthPtr = depthHandle.AddrOfPinnedObject();

        Permissions.RequestPermission(DepthPermission, OnPermissionResult);
    }

    private void OnPermissionResult(string permission, bool granted)
    {
        if (permission != DepthPermission) return;

        permissionGranted = granted;
        if (!granted)
        {
            Debug.LogError("Depth permission denied.");
            return;
        }

        if (started) return; // guard multiple callbacks
        started = true;

        StartDepth();
    }

    private void StartDepth()
    {
        outPath = Path.Combine(Application.persistentDataPath,
            $"depth_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // file header
        bw.Write(new byte[] { (byte)'D',(byte)'E',(byte)'P',(byte)'T',(byte)'H',(byte)'B',(byte)'I',(byte)'N' });
        bw.Write(1); // version

        bool ok = MLDepthNative.MLDepthUnity_Init(STREAM_LONG, FLAG_DEPTH, FPS_5);
        Debug.Log("MLDepthUnity_Init: " + ok);

        if (!ok)
        {
            Debug.LogError("MLDepthUnity_Init failed.");
            CleanupFile();
            return;
        }

        Debug.Log("Depth output file: " + outPath);
    }

    void Update()
    {
        if (!permissionGranted || !started || bw == null) return;

        MLDepthNative.DepthFrameInfo info;
        int bytesWritten;

        bool got = MLDepthNative.MLDepthUnity_TryGetLatestDepth(
            0,
            out info,
            depthPtr,                 // ✅ IntPtr, not byte[]
            depthBuf.Length,
            out bytesWritten
        );

        if (!got || bytesWritten <= 0) return;

        frameIndex++;

        if (Time.frameCount % 60 == 0)
        {
            Debug.Log($"Depth frame #{frameIndex} bytes={bytesWritten} w={info.width} h={info.height} stride={info.strideBytes} t={info.captureTimeNs}");
        }

        // record per frame
        bw.Write(frameIndex);
        bw.Write(info.captureTimeNs);
        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(bytesWritten);
        bw.Write(depthBuf, 0, bytesWritten);

        if ((frameIndex % 30) == 0)
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
            try { MLDepthNative.MLDepthUnity_Shutdown(); } catch { }
        }

        CleanupFile();

        if (depthHandle.IsAllocated)
            depthHandle.Free();
        depthPtr = IntPtr.Zero;
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
}
