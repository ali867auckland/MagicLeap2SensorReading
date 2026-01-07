using System;
using System.Runtime.InteropServices;

public static class MLWorldCamNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string DLL = "mldepth_unity"; // libmldepth_unity.so
#else
    private const string DLL = "__Internal";
#endif

    [StructLayout(LayoutKind.Sequential)]
    public struct WorldCamFrameInfo
    {
        public int camId;
        public long frameNumber;
        public long timestampNs;
        public int width;
        public int height;
        public int strideBytes;
        public int bytesPerPixel;
        public int frameType;
    }

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool MLWorldCamUnity_Init(uint identifiersMask);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool MLWorldCamUnity_TryGetLatest(
        uint timeoutMs,
        out WorldCamFrameInfo outInfo,
        IntPtr outBytes,
        int capacityBytes,
        out int bytesWritten);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLWorldCamUnity_Shutdown();
}
