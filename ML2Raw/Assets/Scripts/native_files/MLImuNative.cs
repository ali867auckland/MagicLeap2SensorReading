using System;
using System.Runtime.InteropServices;

public static class MLIMUNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    [StructLayout(LayoutKind.Sequential)]
    public struct IMUData
    {
        // Accelerometer (m/s²)
        public float accel_x;
        public float accel_y;
        public float accel_z;
        public long accel_timestamp_ns;
        
        // Gyroscope (rad/s)
        public float gyro_x;
        public float gyro_y;
        public float gyro_z;
        public long gyro_timestamp_ns;
        
        // Flags
        public int has_accel;
        public int has_gyro;
    }

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLIMUUnity_Init(int sampleRateHz);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLIMUUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLIMUUnity_TryGetLatest(out IMUData outData);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLIMUUnity_GetBuffered(
        [Out] IMUData[] outData,
        int maxCount,
        out int outCount);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong MLIMUUnity_GetAccelCount();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong MLIMUUnity_GetGyroCount();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLIMUUnity_Shutdown();
}