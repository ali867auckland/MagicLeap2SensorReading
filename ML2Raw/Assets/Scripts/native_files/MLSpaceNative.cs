using System;
using System.Runtime.InteropServices;

public static class MLSpaceNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    // Space localization status (matches MLSpaceLocalizationStatus)
    public enum SpaceLocalizationStatus : uint
    {
        NotLocalized = 0,
        Localized = 1,
        LocalizationPending = 2,
        SleepingBeforeRetry = 3
    }

    // Space localization confidence (matches MLSpaceLocalizationConfidence)
    public enum SpaceLocalizationConfidence : uint
    {
        Poor = 0,
        Fair = 1,
        Good = 2,
        Excellent = 3
    }

    // Space localization error flags - BITMASK
    [Flags]
    public enum SpaceLocalizationErrorFlag : uint
    {
        None = 0,
        Unknown = 1 << 0,
        OutOfMappedArea = 1 << 1,
        LowFeatureCount = 1 << 2,
        ExcessiveMotion = 1 << 3,
        LowLight = 1 << 4,
        HeadposeFailure = 1 << 5,
        AlgorithmFailure = 1 << 6
    }

    // Space type
    public enum SpaceType : uint
    {
        OnDevice = 0,
        ARCloud = 1
    }

    // Space localization data structure
    [StructLayout(LayoutKind.Sequential)]
    public struct SpaceLocalizationData
    {
        // Localization state
        public uint status;           // SpaceLocalizationStatus
        public uint confidence;       // SpaceLocalizationConfidence
        public uint errorFlags;       // Bitmask of SpaceLocalizationErrorFlag
        public uint spaceType;        // SpaceType
        
        // Space identification
        public ulong spaceId_data0;
        public ulong spaceId_data1;
        
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string spaceName;
        
        // Timing
        public long timestampNs;
        
        // Target space origin frame UID
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] targetSpaceOrigin;
        
        // Result code
        public int resultCode;

        // Helpers
        public SpaceLocalizationStatus Status => (SpaceLocalizationStatus)status;
        public SpaceLocalizationConfidence Confidence => (SpaceLocalizationConfidence)confidence;
        public SpaceLocalizationErrorFlag ErrorFlags => (SpaceLocalizationErrorFlag)errorFlags;
        public SpaceType Type => (SpaceType)spaceType;
        public bool IsValid => resultCode == 0;
        public bool IsLocalized => Status == SpaceLocalizationStatus.Localized;
    }

    // Space info for space list
    [StructLayout(LayoutKind.Sequential)]
    public struct SpaceInfo
    {
        public ulong spaceId_data0;
        public ulong spaceId_data1;
        
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string spaceName;
        
        public uint spaceType;        // SpaceType
        public long timestampNs;

        public SpaceType Type => (SpaceType)spaceType;
    }

    // ---------------- Native functions ----------------

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpaceUnity_Init();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpaceUnity_GetLocalizationStatus(out SpaceLocalizationData out_data);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpaceUnity_GetSpaceList(
        [Out] SpaceInfo[] out_spaces, 
        int max_spaces, 
        out int out_count);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpaceUnity_RequestLocalization(
        ulong space_id_data0, 
        ulong space_id_data1);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLSpaceUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLSpaceUnity_Shutdown();
}