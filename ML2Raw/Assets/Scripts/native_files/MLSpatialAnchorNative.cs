using System;
using System.Runtime.InteropServices;

public static class MLSpatialAnchorNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    public enum AnchorQuality : uint
    {
        Low = 0,
        Medium = 1,
        High = 2
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct AnchorCreationResult
    {
        [MarshalAs(UnmanagedType.I1)]
        public bool success;
        public ulong anchorId_data0;
        public ulong anchorId_data1;
        public int resultCode;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct AnchorPoseData
    {
        public ulong anchorId_data0;
        public ulong anchorId_data1;
        
        public float rotation_x, rotation_y, rotation_z, rotation_w;
        public float position_x, position_y, position_z;
        
        public uint quality;
        
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] frameUid;
        
        public long timestampNs;
        public int resultCode;

        public UnityEngine.Quaternion ToQuaternion()
        {
            return new UnityEngine.Quaternion(rotation_x, rotation_y, rotation_z, rotation_w);
        }

        public UnityEngine.Vector3 ToPosition()
        {
            return new UnityEngine.Vector3(position_x, position_y, position_z);
        }

        public AnchorQuality Quality => (AnchorQuality)quality;
        public bool IsValid => resultCode == 0;
    }

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpatialAnchorUnity_Init();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern AnchorCreationResult MLSpatialAnchorUnity_CreateAnchor(
        float rotation_x, float rotation_y, float rotation_z, float rotation_w,
        float position_x, float position_y, float position_z);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpatialAnchorUnity_GetAnchorPose(
        ulong anchor_id_0, ulong anchor_id_1,
        out AnchorPoseData out_pose);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpatialAnchorUnity_GetAllAnchors(
        [Out] AnchorPoseData[] out_poses, int max_count, out int out_count);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern float MLSpatialAnchorUnity_GetDistanceToNearestAnchor(
        float pos_x, float pos_y, float pos_z);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpatialAnchorUnity_DeleteAnchor(
        ulong anchor_id_0, ulong anchor_id_1);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern int MLSpatialAnchorUnity_GetAnchorCount();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLSpatialAnchorUnity_SetAutoCreate(
        [MarshalAs(UnmanagedType.I1)] bool enabled,
        float min_distance,
        uint max_anchors);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLSpatialAnchorUnity_Update();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpatialAnchorUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLSpatialAnchorUnity_Shutdown();
}