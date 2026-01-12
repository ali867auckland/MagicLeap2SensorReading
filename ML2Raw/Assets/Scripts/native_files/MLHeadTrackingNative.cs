using System;
using System.Runtime.InteropServices;

public static class MLHeadTrackingNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mlheadtracking_unity";
#else
    private const string LIB = "__Internal";
#endif

    // Head tracking status enum (matches MLHeadTrackingStatus)
    public enum HeadTrackingStatus : uint
    {
        Invalid = 0,
        Initializing = 1,
        Relocalizing = 2,
        Valid = 100
    }

    // Head tracking error flags - BITMASK (matches MLHeadTrackingErrorFlag)
    [Flags]
    public enum HeadTrackingErrorFlag : uint
    {
        None = 0,
        Unknown = 1 << 0,
        NotEnoughFeatures = 1 << 1,
        LowLight = 1 << 2,
        ExcessiveMotion = 1 << 3
    }

    // Map event flags - BITMASK (matches MLHeadTrackingMapEvent)
    [Flags]
    public enum HeadTrackingMapEvent : ulong
    {
        None = 0,
        Lost = 1 << 0,
        Recovered = 1 << 1,
        RecoveryFailed = 1 << 2,
        NewSession = 1 << 3
    }

    // Head pose data structure matching native layout
    [StructLayout(LayoutKind.Sequential)]
    public struct HeadPoseData
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
        // Tracking state
        public uint status;
        public float confidence;
        public uint errorFlags;
        // Map events (bitmask)
        public ulong mapEventsMask;
        [MarshalAs(UnmanagedType.I1)]
        public bool hasMapEvent;
        // Result
        public int resultCode;

        // Helpers
        public UnityEngine.Quaternion ToQuaternion()
        {
            return new UnityEngine.Quaternion(rotation_x, rotation_y, rotation_z, rotation_w);
        }

        public UnityEngine.Vector3 ToPosition()
        {
            return new UnityEngine.Vector3(position_x, position_y, position_z);
        }

        public bool IsValid => resultCode == 0;

        public HeadTrackingStatus Status => (HeadTrackingStatus)status;
        public HeadTrackingErrorFlag ErrorFlags => (HeadTrackingErrorFlag)errorFlags;
        public HeadTrackingMapEvent MapEvents => (HeadTrackingMapEvent)mapEventsMask;

        // Check specific error flags
        public bool HasError(HeadTrackingErrorFlag flag) => (errorFlags & (uint)flag) != 0;
        
        // Check specific map events
        public bool HasMapEvent(HeadTrackingMapEvent evt) => (mapEventsMask & (ulong)evt) != 0;
    }

    // ---------------- Native functions ----------------

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLHeadTrackingUnity_Init();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLHeadTrackingUnity_GetPose(out HeadPoseData out_pose);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong MLHeadTrackingUnity_GetHandle();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLHeadTrackingUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLHeadTrackingUnity_Shutdown();
}