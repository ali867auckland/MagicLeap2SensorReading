#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mesh block state
typedef enum MeshBlockState {
    MeshBlockState_New = 0,
    MeshBlockState_Updated = 1,
    MeshBlockState_Deleted = 2,
    MeshBlockState_Unchanged = 3
} MeshBlockState;

// Mesh info for a single block
typedef struct MeshBlockInfo {
    uint64_t id_high;   // Block ID (CFUID upper 64 bits)
    uint64_t id_low;    // Block ID (CFUID lower 64 bits)
    float center_x, center_y, center_z;
    float extents_x, extents_y, extents_z;
    int64_t timestampNs;
    int32_t state;      // MeshBlockState
} MeshBlockInfo;

// Summary of mesh data
typedef struct MeshSummary {
    int64_t timestampNs;
    int32_t totalBlocks;
    int32_t newBlocks;
    int32_t updatedBlocks;
    int32_t deletedBlocks;
    int32_t totalVertices;
    int32_t totalTriangles;
} MeshSummary;

// Initialize meshing
// flags: combination of MLMeshingFlags
// fill_hole_length: max hole perimeter to fill (meters, 0-5)
// disconnected_area: min area to keep (meters^2, 0-2)
bool MLMeshingUnity_Init(uint32_t flags, float fill_hole_length, float disconnected_area);

// Set the query region (bounding box around device)
void MLMeshingUnity_SetQueryRegion(float center_x, float center_y, float center_z,
                                    float extent_x, float extent_y, float extent_z);

// Request mesh info update (call periodically)
// Returns true if request was submitted
bool MLMeshingUnity_RequestMeshInfo(void);

// Check if mesh info is ready and get summary
// Returns true if new data available
bool MLMeshingUnity_GetMeshSummary(MeshSummary* out_summary);

// Get block info at index (after GetMeshSummary returns true)
bool MLMeshingUnity_GetBlockInfo(int32_t index, MeshBlockInfo* out_info);

// Request mesh data for specific blocks (by index from block info)
// block_indices: array of indices, count: number of blocks
// lod: 0=Min, 1=Medium, 2=Max
bool MLMeshingUnity_RequestMesh(const int32_t* block_indices, int32_t count, int32_t lod);

// Poll for mesh result (call this while waiting for mesh data)
// Returns true if mesh result was processed (success or failure)
bool MLMeshingUnity_PollMeshResult(void);

// Check if mesh data is ready
// Returns true if ready, out_vertex_count/out_index_count will be set
bool MLMeshingUnity_IsMeshReady(int32_t* out_vertex_count, int32_t* out_index_count);

// Get mesh data (vertices as float xyz, indices as uint16)
// Call after IsMeshReady returns true
bool MLMeshingUnity_GetMeshData(
    float* out_vertices, int32_t vertex_capacity,
    uint16_t* out_indices, int32_t index_capacity,
    float* out_normals,   // Can be null if normals not requested
    float* out_confidence // Can be null if confidence not requested
);

// Check if initialized
bool MLMeshingUnity_IsInitialized(void);

// Shutdown
void MLMeshingUnity_Shutdown(void);

#ifdef __cplusplus
}
#endif