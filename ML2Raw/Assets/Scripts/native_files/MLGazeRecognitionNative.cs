using System;
using System.Runtime.InteropServices;

public static class MLGazeRecognitionNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    // Gaze behavior enum
    public enum GazeBehavior
    {
        Unknown = 0,
        EyesClosed = 1,
        Blink = 2,
        Fixation = 3,
        Pursuit = 4,
        Saccade = 5,
        BlinkLeft = 6,
        BlinkRight = 7
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct GazeRecognitionData
    {
        public long timestampNs;
        
        // Current behavior
        public int behavior;
        
        // Eye-in-skull position (normalized)
        public float eye_left_x, eye_left_y;
        public float eye_right_x, eye_right_y;
        
        // Behavior metadata
        public float onset_s;
        public float duration_s;
        public float velocity_degps;
        public float amplitude_deg;
        public float direction_radial;
        
        // Error state
        public int error;
        
        // Helpers
        public GazeBehavior Behavior => (GazeBehavior)behavior;
        
        public string BehaviorName => Behavior switch
        {
            GazeBehavior.Unknown => "Unknown",
            GazeBehavior.EyesClosed => "EyesClosed",
            GazeBehavior.Blink => "Blink",
            GazeBehavior.Fixation => "Fixation",
            GazeBehavior.Pursuit => "Pursuit",
            GazeBehavior.Saccade => "Saccade",
            GazeBehavior.BlinkLeft => "BlinkLeft",
            GazeBehavior.BlinkRight => "BlinkRight",
            _ => $"Unknown({behavior})"
        };
        
        public UnityEngine.Vector2 EyeLeft => new UnityEngine.Vector2(eye_left_x, eye_left_y);
        public UnityEngine.Vector2 EyeRight => new UnityEngine.Vector2(eye_right_x, eye_right_y);
    }

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLGazeRecognitionUnity_Init();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLGazeRecognitionUnity_GetLatest(out GazeRecognitionData out_data);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLGazeRecognitionUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong MLGazeRecognitionUnity_GetSampleCount();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLGazeRecognitionUnity_Shutdown();
}