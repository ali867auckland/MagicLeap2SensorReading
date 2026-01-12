using System;
using System.Runtime.InteropServices;

public static class MLRGBCameraNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";  // All symbols in libmldepth_unity.so
#else
    private const string LIB = "__Internal";
#endif

    // Capture mode
    public enum RGBCaptureMode : int
    {
        Preview = 0,    // Lower res, higher FPS (640x480)
        Video = 1,      // Video mode (1280x720 @ 30fps)
        Image = 2       // Still image (1920x1080 JPEG)
    }

    // Frame info with synchronized camera pose
    [StructLayout(LayoutKind.Sequential)]
    public struct RGBFrameWithPose
    {
        // Frame info
        public int width;
        public int height;
        public int strideBytes;
        public int format;
        public long timestampNs;
        
        // Camera pose (from CV Camera API)
        public float pose_rotation_x;
        public float pose_rotation_y;
        public float pose_rotation_z;
        public float pose_rotation_w;
        public float pose_position_x;
        public float pose_position_y;
        public float pose_position_z;
        public int pose_valid;
        public int pose_result_code;
        
        // Intrinsics
        public float fx, fy;
        public float cx, cy;

        // Helpers
        public bool HasValidPose => pose_valid != 0;

        public UnityEngine.Quaternion PoseRotation => new UnityEngine.Quaternion(
            pose_rotation_x, pose_rotation_y, pose_rotation_z, pose_rotation_w);

        public UnityEngine.Vector3 PosePosition => new UnityEngine.Vector3(
            pose_position_x, pose_position_y, pose_position_z);

        public double TimestampSec => timestampNs / 1e9;
    }

    // ---------------- Native functions ----------------

    /// <summary>
    /// Initialize RGB camera.
    /// NOTE: CV Camera (CVCameraNativeConsumer) must be initialized first for poses to work.
    /// </summary>
    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLRGBCameraUnity_Init(RGBCaptureMode mode);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLRGBCameraUnity_StartCapture();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLRGBCameraUnity_StopCapture();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLRGBCameraUnity_TryGetLatestFrame(
        uint timeout_ms,
        out RGBFrameWithPose out_info,
        IntPtr out_bytes,
        int capacity_bytes,
        out int out_bytes_written
    );

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong MLRGBCameraUnity_GetFrameCount();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLRGBCameraUnity_IsCapturing();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLRGBCameraUnity_Shutdown();
}