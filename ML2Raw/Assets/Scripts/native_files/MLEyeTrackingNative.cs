using System;
using System.Runtime.InteropServices;

public static class MLEyeTrackingNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    [StructLayout(LayoutKind.Sequential)]
    public struct EyeTrackingData
    {
        public long timestampNs;
        
        // Confidence values (0.0 - 1.0)
        public float vergence_confidence;
        public float left_center_confidence;
        public float right_center_confidence;
        
        // Blink state (1 = blinking)
        public int left_blink;
        public int right_blink;
        
        // Eye openness (0.0 = closed, 1.0 = fully open)
        public float left_eye_openness;
        public float right_eye_openness;
        
        // Gaze direction vectors
        public float left_gaze_x, left_gaze_y, left_gaze_z;
        public float right_gaze_x, right_gaze_y, right_gaze_z;
        
        // Vergence point (where eyes converge)
        public float vergence_x, vergence_y, vergence_z;
        
        // Eye center positions
        public float left_pos_x, left_pos_y, left_pos_z;
        public float right_pos_x, right_pos_y, right_pos_z;
        
        // Error state (0 = none)
        public int error;
        
        // Pose validity flags
        public int vergence_valid;
        public int left_valid;
        public int right_valid;
        
        // Helpers
        public bool LeftBlinking => left_blink != 0;
        public bool RightBlinking => right_blink != 0;
        public bool HasValidVergence => vergence_valid != 0;
        public bool HasValidLeft => left_valid != 0;
        public bool HasValidRight => right_valid != 0;
        
        public UnityEngine.Vector3 LeftGaze => new UnityEngine.Vector3(left_gaze_x, left_gaze_y, left_gaze_z);
        public UnityEngine.Vector3 RightGaze => new UnityEngine.Vector3(right_gaze_x, right_gaze_y, right_gaze_z);
        public UnityEngine.Vector3 VergencePoint => new UnityEngine.Vector3(vergence_x, vergence_y, vergence_z);
    }

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLEyeTrackingUnity_Init();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLEyeTrackingUnity_GetLatest(out EyeTrackingData out_data);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLEyeTrackingUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong MLEyeTrackingUnity_GetSampleCount();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLEyeTrackingUnity_Shutdown();
}