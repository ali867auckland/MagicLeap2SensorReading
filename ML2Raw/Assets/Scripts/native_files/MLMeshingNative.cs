using System;
using System.Runtime.InteropServices;

public static class MLMeshingNative
{
#if UNITY_ANDROID && !UNITY_EDITOR
    private const string LIB = "mldepth_unity";
#else
    private const string LIB = "__Internal";
#endif

    // MLMeshingFlags
    [Flags]
    public enum MeshingFlags : uint
    {
        None = 0,
        PointCloud = 1 << 0,
        ComputeNormals = 1 << 1,
        ComputeConfidence = 1 << 2,
        Planarize = 1 << 3,
        RemoveMeshSkirt = 1 << 4,
        IndexOrderCW = 1 << 5
    }

    // MLMeshingLOD
    public enum MeshingLOD
    {
        Minimum = 0,
        Medium = 1,
        Maximum = 2
    }

    // MeshBlockState
    public enum MeshBlockState
    {
        New = 0,
        Updated = 1,
        Deleted = 2,
        Unchanged = 3
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MeshBlockInfo
    {
        public ulong id_high;
        public ulong id_low;
        public float center_x, center_y, center_z;
        public float extents_x, extents_y, extents_z;
        public long timestampNs;
        public int state;

        public MeshBlockState State => (MeshBlockState)state;
        public UnityEngine.Vector3 Center => new UnityEngine.Vector3(center_x, center_y, center_z);
        public UnityEngine.Vector3 Extents => new UnityEngine.Vector3(extents_x, extents_y, extents_z);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MeshSummary
    {
        public long timestampNs;
        public int totalBlocks;
        public int newBlocks;
        public int updatedBlocks;
        public int deletedBlocks;
        public int totalVertices;
        public int totalTriangles;
    }

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_Init(uint flags, float fill_hole_length, float disconnected_area);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLMeshingUnity_SetQueryRegion(
        float center_x, float center_y, float center_z,
        float extent_x, float extent_y, float extent_z);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_RequestMeshInfo();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_GetMeshSummary(out MeshSummary out_summary);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_GetBlockInfo(int index, out MeshBlockInfo out_info);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_RequestMesh(int[] block_indices, int count, int lod);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_IsMeshReady(out int out_vertex_count, out int out_index_count);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_GetMeshData(
        [Out] float[] out_vertices, int vertex_capacity,
        [Out] ushort[] out_indices, int index_capacity,
        [Out] float[] out_normals,
        [Out] float[] out_confidence);

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool MLMeshingUnity_IsInitialized();

    [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
    public static extern void MLMeshingUnity_Shutdown();
}