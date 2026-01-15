#include "mlmeshing.h"
#include "mlperception_service.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>

#include <android/log.h>
#include <ml_meshing2.h>

#define LOG_TAG "MLMeshingUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

static bool g_debug = true;

static std::mutex g_mutex;
static std::atomic<bool> g_initialized{false};

static MLHandle g_meshClient = ML_INVALID_HANDLE;
static MLHandle g_meshInfoRequest = ML_INVALID_HANDLE;
static MLHandle g_meshDataRequest = ML_INVALID_HANDLE;

// Query region
static MLMeshingExtents g_queryExtents;

// Cached mesh info
static std::vector<MLMeshingBlockInfo> g_blockInfos;
static MLMeshingMeshInfo g_meshInfo;
static bool g_hasMeshInfo = false;

// Cached mesh data
static MLMeshingMesh g_meshData;
static bool g_hasMeshData = false;
static std::vector<float> g_vertices;
static std::vector<uint16_t> g_indices;
static std::vector<float> g_normals;
static std::vector<float> g_confidence;

bool MLMeshingUnity_Init(uint32_t flags, float fill_hole_length, float disconnected_area) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }
    
    // Initialize settings
    MLMeshingSettings settings;
    MLResult r = MLMeshingInitSettings(&settings);
    if (r != MLResult_Ok) {
        LOGE("MLMeshingInitSettings failed r=%d", (int)r);
        return false;
    }
    
    settings.flags = flags;
    settings.fill_hole_length = fill_hole_length;
    settings.disconnected_component_area = disconnected_area;
    
    // Create client
    r = MLMeshingCreateClient(&g_meshClient, &settings);
    if (r != MLResult_Ok) {
        LOGE("MLMeshingCreateClient failed r=%d", (int)r);
        return false;
    }
    
    LOGI("Meshing client created handle=%llu flags=%u", 
         (unsigned long long)g_meshClient, flags);
    
    // Default query region (10m cube around origin)
    g_queryExtents.center = {0, 0, 0};
    g_queryExtents.rotation = {0, 0, 0, 1};
    g_queryExtents.extents = {10.0f, 10.0f, 10.0f};
    
    g_initialized.store(true);
    
    LOGI("Meshing initialized");
    return true;
}

void MLMeshingUnity_SetQueryRegion(float center_x, float center_y, float center_z,
                                    float extent_x, float extent_y, float extent_z) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    g_queryExtents.center = {center_x, center_y, center_z};
    g_queryExtents.extents = {extent_x, extent_y, extent_z};
}

bool MLMeshingUnity_RequestMeshInfo(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized.load()) return false;
    
    // If we have a pending request, check if it's done first
    if (g_meshInfoRequest != ML_INVALID_HANDLE) {
        MLMeshingMeshInfo info;
        MLResult r = MLMeshingGetMeshInfoResult(g_meshClient, g_meshInfoRequest, &info);
        
        if (r == MLResult_Ok) {
            // Store block infos
            g_blockInfos.clear();
            for (uint32_t i = 0; i < info.data_count; i++) {
                g_blockInfos.push_back(info.data[i]);
            }
            g_meshInfo = info;
            g_hasMeshInfo = true;
            
            // Free the request
            MLMeshingFreeResource(g_meshClient, &g_meshInfoRequest);
            g_meshInfoRequest = ML_INVALID_HANDLE;
            
            if (g_debug) {
                int newCount = 0, updatedCount = 0, deletedCount = 0;
                for (const auto& block : g_blockInfos) {
                    if (block.state == MLMeshingMeshState_New) newCount++;
                    else if (block.state == MLMeshingMeshState_Updated) updatedCount++;
                    else if (block.state == MLMeshingMeshState_Deleted) deletedCount++;
                }
                LOGI("Mesh info: %zu blocks (new=%d, updated=%d, deleted=%d)",
                     g_blockInfos.size(), newCount, updatedCount, deletedCount);
            }
        } else if (r != MLResult_Pending) {
            // Error, clear request
            MLMeshingFreeResource(g_meshClient, &g_meshInfoRequest);
            g_meshInfoRequest = ML_INVALID_HANDLE;
        }
        // If pending, just wait
        return false;
    }
    
    // Submit new request
    MLResult r = MLMeshingRequestMeshInfo(g_meshClient, &g_queryExtents, &g_meshInfoRequest);
    if (r != MLResult_Ok) {
        LOGW("MLMeshingRequestMeshInfo failed r=%d", (int)r);
        return false;
    }
    
    return true;
}

bool MLMeshingUnity_GetMeshSummary(MeshSummary* out_summary) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!out_summary || !g_hasMeshInfo) return false;
    
    memset(out_summary, 0, sizeof(MeshSummary));
    out_summary->timestampNs = g_meshInfo.timestamp;
    out_summary->totalBlocks = (int32_t)g_blockInfos.size();
    
    for (const auto& block : g_blockInfos) {
        switch (block.state) {
            case MLMeshingMeshState_New: out_summary->newBlocks++; break;
            case MLMeshingMeshState_Updated: out_summary->updatedBlocks++; break;
            case MLMeshingMeshState_Deleted: out_summary->deletedBlocks++; break;
            default: break;
        }
    }
    
    // If we have mesh data, include totals
    if (g_hasMeshData) {
        out_summary->totalVertices = (int32_t)g_vertices.size() / 3;
        out_summary->totalTriangles = (int32_t)g_indices.size() / 3;
    }
    
    g_hasMeshInfo = false; // Consume the data
    return true;
}

bool MLMeshingUnity_GetBlockInfo(int32_t index, MeshBlockInfo* out_info) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!out_info || index < 0 || index >= (int32_t)g_blockInfos.size()) {
        return false;
    }
    
    const MLMeshingBlockInfo& block = g_blockInfos[index];
    
    // Copy CFUID as two 64-bit values
    memcpy(&out_info->id_high, &block.id.data[0], 8);
    memcpy(&out_info->id_low, &block.id.data[8], 8);
    
    out_info->center_x = block.extents.center.x;
    out_info->center_y = block.extents.center.y;
    out_info->center_z = block.extents.center.z;
    out_info->extents_x = block.extents.extents.x;
    out_info->extents_y = block.extents.extents.y;
    out_info->extents_z = block.extents.extents.z;
    out_info->timestampNs = block.timestamp;
    out_info->state = (int32_t)block.state;
    
    return true;
}

bool MLMeshingUnity_PollMeshResult(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized.load() || g_meshDataRequest == ML_INVALID_HANDLE) {
        return false;
    }
    
    MLMeshingMesh mesh;
    MLResult r = MLMeshingGetMeshResult(g_meshClient, g_meshDataRequest, &mesh);
    
    if (r == MLResult_Pending) {
        // Still waiting
        return false;
    }
    
    if (r != MLResult_Ok) {
        LOGW("MLMeshingGetMeshResult failed r=%d", (int)r);
        MLMeshingFreeResource(g_meshClient, &g_meshDataRequest);
        g_meshDataRequest = ML_INVALID_HANDLE;
        return true; // Request completed (with error)
    }
    
    // Collect all vertices and indices
    g_vertices.clear();
    g_indices.clear();
    g_normals.clear();
    g_confidence.clear();
    
    for (uint32_t i = 0; i < mesh.data_count; i++) {
        const MLMeshingBlockMesh& block = mesh.data[i];
        if (block.result != MLMeshingResult_Success) {
            if (g_debug) {
                LOGW("Block %u result=%d", i, (int)block.result);
            }
            continue;
        }
        
        uint32_t vertexOffset = (uint32_t)(g_vertices.size() / 3);
        
        // Add vertices
        for (uint32_t v = 0; v < block.vertex_count; v++) {
            g_vertices.push_back(block.vertex[v].x);
            g_vertices.push_back(block.vertex[v].y);
            g_vertices.push_back(block.vertex[v].z);
        }
        
        // Add indices (adjusted for vertex offset)
        for (uint16_t idx = 0; idx < block.index_count; idx++) {
            g_indices.push_back(block.index[idx] + vertexOffset);
        }
        
        // Add normals if available
        if (block.normal) {
            for (uint32_t v = 0; v < block.vertex_count; v++) {
                g_normals.push_back(block.normal[v].x);
                g_normals.push_back(block.normal[v].y);
                g_normals.push_back(block.normal[v].z);
            }
        }
        
        // Add confidence if available
        if (block.confidence) {
            for (uint32_t v = 0; v < block.vertex_count; v++) {
                g_confidence.push_back(block.confidence[v]);
            }
        }
    }
    
    g_meshData = mesh;
    g_hasMeshData = true;
    
    // Free the request
    MLMeshingFreeResource(g_meshClient, &g_meshDataRequest);
    g_meshDataRequest = ML_INVALID_HANDLE;
    
    if (g_debug) {
        LOGI("Mesh data ready: %zu vertices, %zu indices (%zu triangles), %zu blocks processed",
             g_vertices.size() / 3, g_indices.size(), g_indices.size() / 3, (size_t)mesh.data_count);
    }
    
    return true; // Request completed successfully
}

bool MLMeshingUnity_RequestMesh(const int32_t* block_indices, int32_t count, int32_t lod) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized.load() || count <= 0 || !block_indices) {
        return false;
    }
    
    // Don't submit if we already have a pending request
    if (g_meshDataRequest != ML_INVALID_HANDLE) {
        LOGW("Mesh request already pending, wait for it to complete");
        return false;
    }
    
    // Build request
    std::vector<MLMeshingBlockRequest> requests;
    for (int32_t i = 0; i < count; i++) {
        int32_t idx = block_indices[i];
        if (idx >= 0 && idx < (int32_t)g_blockInfos.size()) {
            MLMeshingBlockRequest req;
            req.id = g_blockInfos[idx].id;
            req.level = (MLMeshingLOD)lod;
            requests.push_back(req);
        }
    }
    
    if (requests.empty()) {
        LOGW("No valid blocks to request");
        return false;
    }
    
    MLMeshingMeshRequest meshRequest;
    meshRequest.request_count = (int)requests.size();
    meshRequest.data = requests.data();
    
    MLResult r = MLMeshingRequestMesh(g_meshClient, &meshRequest, &g_meshDataRequest);
    if (r != MLResult_Ok) {
        LOGW("MLMeshingRequestMesh failed r=%d", (int)r);
        return false;
    }
    
    if (g_debug) {
        LOGI("Mesh request submitted: %zu blocks, LOD=%d", requests.size(), lod);
    }
    
    return true;
}

bool MLMeshingUnity_IsMeshReady(int32_t* out_vertex_count, int32_t* out_index_count) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_hasMeshData) return false;
    
    if (out_vertex_count) *out_vertex_count = (int32_t)(g_vertices.size() / 3);
    if (out_index_count) *out_index_count = (int32_t)g_indices.size();
    
    return true;
}

bool MLMeshingUnity_GetMeshData(
    float* out_vertices, int32_t vertex_capacity,
    uint16_t* out_indices, int32_t index_capacity,
    float* out_normals,
    float* out_confidence
) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_hasMeshData) return false;
    
    int32_t vertexCount = (int32_t)(g_vertices.size() / 3);
    int32_t indexCount = (int32_t)g_indices.size();
    
    if (vertex_capacity < vertexCount * 3 || index_capacity < indexCount) {
        LOGW("Buffer too small: need %d verts, %d indices", vertexCount * 3, indexCount);
        return false;
    }
    
    // Copy data
    if (out_vertices) {
        memcpy(out_vertices, g_vertices.data(), g_vertices.size() * sizeof(float));
    }
    
    if (out_indices) {
        memcpy(out_indices, g_indices.data(), g_indices.size() * sizeof(uint16_t));
    }
    
    if (out_normals && !g_normals.empty()) {
        memcpy(out_normals, g_normals.data(), g_normals.size() * sizeof(float));
    }
    
    if (out_confidence && !g_confidence.empty()) {
        memcpy(out_confidence, g_confidence.data(), g_confidence.size() * sizeof(float));
    }
    
    g_hasMeshData = false; // Consume
    return true;
}

bool MLMeshingUnity_IsInitialized(void) {
    return g_initialized.load();
}

void MLMeshingUnity_Shutdown(void) {
    LOGI("Shutting down Meshing...");
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    // Free pending requests
    if (g_meshInfoRequest != ML_INVALID_HANDLE) {
        MLMeshingFreeResource(g_meshClient, &g_meshInfoRequest);
        g_meshInfoRequest = ML_INVALID_HANDLE;
    }
    
    if (g_meshDataRequest != ML_INVALID_HANDLE) {
        MLMeshingFreeResource(g_meshClient, &g_meshDataRequest);
        g_meshDataRequest = ML_INVALID_HANDLE;
    }
    
    if (g_meshClient != ML_INVALID_HANDLE) {
        MLMeshingDestroyClient(g_meshClient);
        g_meshClient = ML_INVALID_HANDLE;
    }
    
    g_blockInfos.clear();
    g_vertices.clear();
    g_indices.clear();
    g_normals.clear();
    g_confidence.clear();
    g_hasMeshInfo = false;
    g_hasMeshData = false;
    
    g_initialized.store(false);
    
    LOGI("Meshing shutdown complete");
}