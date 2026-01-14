using System;
using System.Runtime.InteropServices;

public static class MLWorldCamNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string DLL = "mldepth_unity"; // libmldepth_unity.so
#else
    private const string DLL = "__Internal";
#endif

    // Camera identifiers (bitmask)
    public const uint CAM_LEFT   = 1u << 0;  // 1
    public const uint CAM_RIGHT  = 1u << 1;  // 2
    public const uint CAM_CENTER = 1u << 2;  // 4
    public const uint CAM_ALL    = CAM_LEFT | CAM_RIGHT | CAM_CENTER; // 7

    [StructLayout(LayoutKind.Sequential)]
    public struct WorldCamFrameInfo
    {
        public int camId;
        public int width;
        public int height;
        public int strideBytes;
        public int bytesPerPixel;
        public int frameType;
        public long timestampNs;

        public string CameraName => camId switch
        {
            1 => "LEFT",
            2 => "RIGHT",
            4 => "CENTER",
            _ => $"UNKNOWN({camId})"
        };
    }

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLWorldCamUnity_Init(uint identifiersMask);

    // Get frame from specific camera (camId = 1=LEFT, 2=RIGHT, 4=CENTER)
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLWorldCamUnity_TryGetLatest(
        uint camId,
        out WorldCamFrameInfo outInfo,
        IntPtr outBytes,
        int capacityBytes,
        out int bytesWritten);

    // Get count of cameras with new frames available
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int MLWorldCamUnity_GetAvailableCount();

    // Check if specific camera has new frame
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLWorldCamUnity_HasNewFrame(uint camId);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLWorldCamUnity_Shutdown();
}