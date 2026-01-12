using System;
using System.Runtime.InteropServices;

public static class MLDepthNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    [StructLayout(LayoutKind.Sequential)]
    public struct DepthFrameInfo
    {
        public int width;
        public int height;
        public int strideBytes;
        public long captureTimeNs;
        public int bytesPerPixel;
        public int format;
    }

    // ---------------- Perception service (native) ----------------

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLPerceptionService_Startup();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLPerceptionService_StartupAndWait(uint timeoutMs);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLPerceptionService_Shutdown();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLPerceptionService_IsStarted();

    // ---------------- Depth native plugin ----------------

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]

    public static extern bool MLDepthUnity_Init(uint streamMask, uint flagsMask, uint frameRateEnum);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLDepthUnity_TryGetLatestDepth(
        uint timeoutMs,
        out DepthFrameInfo info,
        IntPtr outBytes,
        int capacityBytes,
        out int bytesWritten
    );

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLDepthUnity_TryGetLatestConfidence(
        out DepthFrameInfo info,
        IntPtr outBytes,
        int capacityBytes,
        out int bytesWritten
    );

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLDepthUnity_TryGetLatestDepthFlags(
        out DepthFrameInfo info,
        IntPtr outBytes,
        int capacityBytes,
        out int bytesWritten
    );

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLDepthUnity_TryGetLatestRawDepth(
        out DepthFrameInfo info,
        IntPtr outBytes,
        int capacityBytes,
        out int bytesWritten
    );

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLDepthUnity_TryGetLatestAmbientRawDepth(
        out DepthFrameInfo info,
        IntPtr outBytes,
        int capacityBytes,
        out int bytesWritten
    );

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLDepthUnity_Shutdown();
}
