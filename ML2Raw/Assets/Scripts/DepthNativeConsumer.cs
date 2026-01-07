using System;
using System.IO;
using System.Runtime.InteropServices;
using MagicLeap.Android;
using UnityEngine;

public class DepthNativeConsumer : MonoBehaviour
{
    private const string DepthPermission = "com.magicleap.permission.DEPTH_CAMERA";

    // Streams
    private const uint STREAM_LONG  = 1u << 0;
    private const uint STREAM_SHORT = 1u << 1;

    // Flags
    private const uint FLAG_DEPTH        = 1u << 0; // DepthImage
    private const uint FLAG_CONF         = 1u << 1; // Confidence
    private const uint FLAG_DFLAGS       = 1u << 2; // DepthFlags
    private const uint FLAG_AMBIENT_RAW  = 1u << 3; // AmbientRawDepthImage
    private const uint FLAG_RAW          = 1u << 4; // RawDepthImage

    // FPS
    private const uint FPS_25 = 2; // MLDepthCameraFrameRate_25FPS

    private byte[] buf = new byte[8 * 1024 * 1024];
    private GCHandle bufHandle;
    private IntPtr bufPtr = IntPtr.Zero;

    private bool started = false;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    private uint frameIndex = 0;

    void Start()
    {
        bufHandle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        bufPtr = bufHandle.AddrOfPinnedObject();

        Permissions.RequestPermission(DepthPermission, OnGranted);
    }

    private void OnGranted(string perm)
    {
        if (perm != DepthPermission) return;
        if (started) return;
        started = true;

        outPath = Path.Combine(Application.persistentDataPath,
            $"depth_raw_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        bw.Write(new byte[] { (byte)'D',(byte)'E',(byte)'P',(byte)'T',(byte)'H',(byte)'R',(byte)'A',(byte)'W' });
        bw.Write(1); // version

        uint streams = STREAM_LONG; // switch to STREAM_SHORT if you want close-range optimized
        uint flags = FLAG_DEPTH | FLAG_CONF | FLAG_DFLAGS | FLAG_RAW | FLAG_AMBIENT_RAW;

        bool ok = MLDepthNative.MLDepthUnity_Init(streams, flags, FPS_25);
        Debug.Log($"MLDepthUnity_Init(raw): {ok} streams={streams} flags={flags} fps=25");

        if (!ok)
        {
            Debug.LogError("MLDepthUnity_Init failed.");
            CleanupFile();
            enabled = false;
            return;
        }

        Debug.Log("Depth output file: " + outPath);
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Always anchor a frame on processed depth (so we have a consistent “frame header”)
        if (!WriteProcessedDepthFrameHeaderAndBlock())
            return;

        WriteOptionalBlock(2, MLDepthNative.MLDepthUnity_TryGetLatestConfidence);
        WriteOptionalBlock(3, MLDepthNative.MLDepthUnity_TryGetLatestDepthFlags);
        WriteOptionalBlock(4, MLDepthNative.MLDepthUnity_TryGetLatestRawDepth);
        WriteOptionalBlock(5, MLDepthNative.MLDepthUnity_TryGetLatestAmbientRawDepth);

        if ((frameIndex % 30) == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    private bool WriteProcessedDepthFrameHeaderAndBlock()
    {
        MLDepthNative.DepthFrameInfo info;
        int bytesWritten;

        bool got = MLDepthNative.MLDepthUnity_TryGetLatestDepth(
            0,
            out info,
            bufPtr,
            buf.Length,
            out bytesWritten
        );

        if (!got || bytesWritten <= 0) return false;

        frameIndex++;

        if (Time.frameCount % 60 == 0)
        {
            Debug.Log($"Depth OK frame={frameIndex} bytes={bytesWritten} w={info.width} h={info.height} stride={info.strideBytes} bpp={info.bytesPerPixel} t={info.captureTimeNs}");
        }

        // Frame header
        bw.Write(frameIndex);
        bw.Write(info.captureTimeNs);

        // Block 1 = processed depth (with its own info so you can decode later)
        bw.Write((byte)1);
        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(bytesWritten);
        bw.Write(buf, 0, bytesWritten);

        return true;
    }

    private delegate bool Getter(out MLDepthNative.DepthFrameInfo info, IntPtr outBytes, int capacityBytes, out int bytesWritten);

    private void WriteOptionalBlock(byte blockType, Getter getter)
    {
        MLDepthNative.DepthFrameInfo info;
        int bytesWritten;

        bool got = getter(out info, bufPtr, buf.Length, out bytesWritten);

        bw.Write(blockType);

        if (!got || bytesWritten <= 0)
        {
            // mark empty
            bw.Write(0); // width
            bw.Write(0); // height
            bw.Write(0); // stride
            bw.Write(0); // bpp
            bw.Write(0); // nbytes
            return;
        }

        bw.Write(info.width);
        bw.Write(info.height);
        bw.Write(info.strideBytes);
        bw.Write(info.bytesPerPixel);
        bw.Write(bytesWritten);
        bw.Write(buf, 0, bytesWritten);
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

        if (bufHandle.IsAllocated) bufHandle.Free();
        bufPtr = IntPtr.Zero;
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
