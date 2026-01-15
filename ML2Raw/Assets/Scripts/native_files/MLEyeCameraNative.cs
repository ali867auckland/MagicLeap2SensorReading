using System;
using System.Runtime.InteropServices;

public static class MLEyeCameraNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    public const uint CAM_LEFT_TEMPLE  = 1u << 0;
    public const uint CAM_LEFT_NASAL   = 1u << 1;
    public const uint CAM_RIGHT_NASAL  = 1u << 2;
    public const uint CAM_RIGHT_TEMPLE = 1u << 3;
    public const uint CAM_ALL = CAM_LEFT_TEMPLE | CAM_LEFT_NASAL | CAM_RIGHT_NASAL | CAM_RIGHT_TEMPLE;

    [StructLayout(LayoutKind.Sequential)]
    public struct EyeCameraFrameInfo
    {
        public uint camera_id;
        public long frame_number;
        public long timestamp_ns;
        public uint width;
        public uint height;
        public uint stride;
        public uint bytes_per_pixel;
        public uint size;

        public string CameraName => camera_id switch
        {
            1 => "LeftTemple",
            2 => "LeftNasal",
            4 => "RightNasal",
            8 => "RightTemple",
            _ => $"Unknown({camera_id})"
        };

        public double TimestampSec => timestamp_ns / 1e9;
    }

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLEyeCameraUnity_Init(uint camera_mask);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLEyeCameraUnity_TryGetLatestFrame(
        uint camera_id,
        uint timeout_ms,
        out EyeCameraFrameInfo out_info,
        IntPtr out_bytes,
        int capacity_bytes,
        out int bytes_written);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLEyeCameraUnity_HasNewFrame(uint camera_id);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong MLEyeCameraUnity_GetFrameCount(uint camera_id);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLEyeCameraUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLEyeCameraUnity_Shutdown();
}