using System;
using System.Runtime.InteropServices;

public static class MLCVCameraNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    // Camera ID enum
    public enum CVCameraID : int
    {
        ColorCamera = 0
    }

    // Pose structure
    [StructLayout(LayoutKind.Sequential)]
    public struct CVCameraPose
    {
        // Rotation quaternion
        public float rotation_x;
        public float rotation_y;
        public float rotation_z;
        public float rotation_w;
        // Position
        public float position_x;
        public float position_y;
        public float position_z;
        // Timestamp
        public long timestampNs;
        // Result
        public int resultCode;

        public UnityEngine.Quaternion ToQuaternion()
        {
            return new UnityEngine.Quaternion(rotation_x, rotation_y, rotation_z, rotation_w);
        }

        public UnityEngine.Vector3 ToPosition()
        {
            return new UnityEngine.Vector3(position_x, position_y, position_z);
        }

        public bool IsValid => resultCode == 0;
    }

    // ---------------- Native functions ----------------

    /// <summary>
    /// Initialize CV Camera with head tracking handle.
    /// MUST call HeadTrackingNativeConsumer.GetHandle() and pass it here.
    /// </summary>
    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLCVCameraUnity_Init(ulong head_tracking_handle);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLCVCameraUnity_GetPose(
        long timestamp_ns,
        CVCameraID camera_id,
        out CVCameraPose out_pose
    );

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern long MLCVCameraUnity_GetCurrentTimeNs();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLCVCameraUnity_Shutdown();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLCVCameraUnity_IsInitialized();
}