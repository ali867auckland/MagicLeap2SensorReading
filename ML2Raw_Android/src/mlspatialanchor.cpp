#include "mlspatialanchor.h"
#include "mlperception_service.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>
#include <cmath>

#include <android/log.h>
#include <ml_spatial_anchor.h>
#include <ml_perception.h>
#include <ml_snapshot.h>

#define LOG_TAG "MLSpatialAnchorUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

static bool g_debug = true;

static std::mutex g_lock;
static std::atomic<bool> g_initialized{false};

static MLHandle g_trackerHandle = ML_INVALID_HANDLE;

struct AnchorData {
    MLUUID id;
    MLCoordinateFrameUID cfuid;
    float creation_pos_x, creation_pos_y, creation_pos_z;
};

static std::vector<AnchorData> g_anchors;
static float g_minDistance = 0.5f;
static uint32_t g_maxAnchors = 100;
static bool g_autoCreate = false;

static const char* ResultToString(MLResult r) {
    switch (r) {
        case MLResult_Ok: return "Ok";
        case MLResult_InvalidParam: return "InvalidParam";
        case MLResult_UnspecifiedFailure: return "UnspecifiedFailure";
        default: return "Unknown";
    }
}

static void UUIDToUint64(const MLUUID& uuid, uint64_t* out_0, uint64_t* out_1) {
    std::memcpy(out_0, &uuid.data[0], 8);
    std::memcpy(out_1, &uuid.data[8], 8);
}

static void Uint64ToUUID(uint64_t data_0, uint64_t data_1, MLUUID* out_uuid) {
    std::memcpy(&out_uuid->data[0], &data_0, 8);
    std::memcpy(&out_uuid->data[8], &data_1, 8);
}

static float Distance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

static bool GetHeadPosition(float* out_x, float* out_y, float* out_z) {
    // Head tracking is deprecated and not needed for manual anchor creation
    // Auto-create feature disabled - user can manually create anchors as needed
    return false;
}

bool MLSpatialAnchorUnity_Init(void) {
    std::lock_guard<std::mutex> guard(g_lock);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }

    MLResult r = MLSpatialAnchorTrackerCreate(&g_trackerHandle);
    if (r != MLResult_Ok || g_trackerHandle == ML_INVALID_HANDLE) {
        LOGE("MLSpatialAnchorTrackerCreate FAILED r=%d (%s)", (int)r, ResultToString(r));
        return false;
    }

    if (g_debug) {
        LOGI("MLSpatialAnchorTrackerCreate OK handle=%llu", (unsigned long long)g_trackerHandle);
    }

    g_initialized.store(true);
    LOGI("Spatial anchor tracker initialized");
    return true;
}

AnchorCreationResult MLSpatialAnchorUnity_CreateAnchor(
    float rotation_x, float rotation_y, float rotation_z, float rotation_w,
    float position_x, float position_y, float position_z)
{
    AnchorCreationResult result;
    std::memset(&result, 0, sizeof(AnchorCreationResult));
    result.success = false;

    if (!g_initialized.load()) {
        result.resultCode = (int32_t)MLResult_UnspecifiedFailure;
        return result;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    if (g_anchors.size() >= g_maxAnchors) {
        LOGW("Max anchors reached (%u)", g_maxAnchors);
        result.resultCode = (int32_t)MLResult_UnspecifiedFailure;
        return result;
    }

    MLSpatialAnchorCreateInfo createInfo;
    MLSpatialAnchorCreateInfoInit(&createInfo);

    createInfo.transform.position.x = position_x;
    createInfo.transform.position.y = position_y;
    createInfo.transform.position.z = position_z;
    createInfo.transform.rotation.x = rotation_x;
    createInfo.transform.rotation.y = rotation_y;
    createInfo.transform.rotation.z = rotation_z;
    createInfo.transform.rotation.w = rotation_w;

    MLSpatialAnchor anchor;
    MLSpatialAnchorInit(&anchor);

    MLResult r = MLSpatialAnchorCreate(g_trackerHandle, &createInfo, &anchor);
    if (r != MLResult_Ok) {
        LOGE("MLSpatialAnchorCreate FAILED r=%d (%s)", (int)r, ResultToString(r));
        result.resultCode = (int32_t)r;
        return result;
    }

    AnchorData data;
    data.id = anchor.id;
    data.cfuid = anchor.cfuid;
    data.creation_pos_x = position_x;
    data.creation_pos_y = position_y;
    data.creation_pos_z = position_z;

    g_anchors.push_back(data);

    UUIDToUint64(anchor.id, &result.anchorId_data0, &result.anchorId_data1);
    result.success = true;
    result.resultCode = (int32_t)MLResult_Ok;

    if (g_debug) {
        LOGI("Anchor created: %016llx%016llx at (%.2f, %.2f, %.2f)", 
             (unsigned long long)result.anchorId_data0, 
             (unsigned long long)result.anchorId_data1,
             position_x, position_y, position_z);
    }

    return result;
}

bool MLSpatialAnchorUnity_GetAnchorPose(
    uint64_t anchor_id_0, uint64_t anchor_id_1,
    AnchorPoseData* out_pose)
{
    if (!out_pose) return false;

    std::memset(out_pose, 0, sizeof(AnchorPoseData));
    out_pose->rotation_w = 1.0f;
    out_pose->anchorId_data0 = anchor_id_0;
    out_pose->anchorId_data1 = anchor_id_1;

    if (!g_initialized.load()) {
        out_pose->resultCode = (int32_t)MLResult_UnspecifiedFailure;
        return false;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    MLUUID searchId;
    Uint64ToUUID(anchor_id_0, anchor_id_1, &searchId);

    AnchorData* anchorData = nullptr;
    for (auto& a : g_anchors) {
        if (std::memcmp(&a.id, &searchId, sizeof(MLUUID)) == 0) {
            anchorData = &a;
            break;
        }
    }

    if (!anchorData) {
        out_pose->resultCode = (int32_t)MLResult_InvalidParam;
        return false;
    }

    MLSnapshot* snapshot = nullptr;
    MLResult r = MLPerceptionGetSnapshot(&snapshot);
    if (r != MLResult_Ok || snapshot == nullptr) {
        out_pose->resultCode = (int32_t)r;
        return false;
    }

    MLTransform transform;
    std::memset(&transform, 0, sizeof(MLTransform));
    transform.rotation.w = 1.0f;

    r = MLSnapshotGetTransform(snapshot, &anchorData->cfuid, &transform);
    MLPerceptionReleaseSnapshot(snapshot);

    out_pose->resultCode = (int32_t)r;

    if (r != MLResult_Ok) {
        return false;
    }

    out_pose->rotation_x = transform.rotation.x;
    out_pose->rotation_y = transform.rotation.y;
    out_pose->rotation_z = transform.rotation.z;
    out_pose->rotation_w = transform.rotation.w;
    out_pose->position_x = transform.position.x;
    out_pose->position_y = transform.position.y;
    out_pose->position_z = transform.position.z;

    std::memcpy(out_pose->frameUid, &anchorData->cfuid, 16);
    out_pose->quality = 1;

    return true;
}

bool MLSpatialAnchorUnity_GetAllAnchors(
    AnchorPoseData* out_poses, int32_t max_count, int32_t* out_count)
{
    if (!out_poses || !out_count || max_count <= 0) return false;

    *out_count = 0;

    if (!g_initialized.load()) return false;

    std::lock_guard<std::mutex> guard(g_lock);

    int32_t count = (int32_t)g_anchors.size();
    if (count > max_count) count = max_count;

    for (int32_t i = 0; i < count; i++) {
        uint64_t id0, id1;
        UUIDToUint64(g_anchors[i].id, &id0, &id1);
        MLSpatialAnchorUnity_GetAnchorPose(id0, id1, &out_poses[i]);
    }

    *out_count = count;
    return true;
}

float MLSpatialAnchorUnity_GetDistanceToNearestAnchor(
    float pos_x, float pos_y, float pos_z)
{
    if (!g_initialized.load()) return -1.0f;

    std::lock_guard<std::mutex> guard(g_lock);

    if (g_anchors.empty()) return -1.0f;

    float minDist = INFINITY;

    for (const auto& anchor : g_anchors) {
        float dist = Distance3D(
            pos_x, pos_y, pos_z,
            anchor.creation_pos_x, anchor.creation_pos_y, anchor.creation_pos_z
        );
        if (dist < minDist) {
            minDist = dist;
        }
    }

    return minDist;
}

bool MLSpatialAnchorUnity_DeleteAnchor(
    uint64_t anchor_id_0, uint64_t anchor_id_1)
{
    if (!g_initialized.load()) return false;

    std::lock_guard<std::mutex> guard(g_lock);

    MLUUID searchId;
    Uint64ToUUID(anchor_id_0, anchor_id_1, &searchId);

    for (auto it = g_anchors.begin(); it != g_anchors.end(); ++it) {
        if (std::memcmp(&it->id, &searchId, sizeof(MLUUID)) == 0) {
            MLResult r = MLSpatialAnchorDelete(g_trackerHandle, searchId);
            if (g_debug) {
                LOGI("Delete anchor %016llx%016llx: r=%d (%s)",
                     (unsigned long long)anchor_id_0,
                     (unsigned long long)anchor_id_1,
                     (int)r, ResultToString(r));
            }

            g_anchors.erase(it);
            return (r == MLResult_Ok);
        }
    }

    return false;
}

int32_t MLSpatialAnchorUnity_GetAnchorCount(void) {
    if (!g_initialized.load()) return 0;

    std::lock_guard<std::mutex> guard(g_lock);
    return (int32_t)g_anchors.size();
}

void MLSpatialAnchorUnity_SetAutoCreate(bool enabled, float min_distance, uint32_t max_anchors) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_autoCreate = enabled;
    g_minDistance = min_distance;
    g_maxAnchors = max_anchors;

    if (g_debug) {
        LOGI("Auto-create: %s, min_dist=%.2f, max=%u",
             enabled ? "ENABLED" : "DISABLED",
             min_distance, max_anchors);
    }
}

void MLSpatialAnchorUnity_Update(void) {
    if (!g_initialized.load()) return;
    if (!g_autoCreate) return;

    std::lock_guard<std::mutex> guard(g_lock);

    if (g_anchors.size() >= g_maxAnchors) return;

    float head_x, head_y, head_z;
    if (!GetHeadPosition(&head_x, &head_y, &head_z)) {
        return;
    }

    float nearestDist = INFINITY;
    for (const auto& anchor : g_anchors) {
        float dist = Distance3D(
            head_x, head_y, head_z,
            anchor.creation_pos_x, anchor.creation_pos_y, anchor.creation_pos_z
        );
        if (dist < nearestDist) {
            nearestDist = dist;
        }
    }

    if (nearestDist >= g_minDistance) {
        AnchorCreationResult result = MLSpatialAnchorUnity_CreateAnchor(
            0.0f, 0.0f, 0.0f, 1.0f,
            head_x, head_y, head_z
        );

        if (result.success && g_debug) {
            LOGI("Auto-created anchor at (%.2f, %.2f, %.2f), count=%zu",
                 head_x, head_y, head_z, g_anchors.size());
        }
    }
}

bool MLSpatialAnchorUnity_IsInitialized(void) {
    return g_initialized.load();
}

void MLSpatialAnchorUnity_Shutdown(void) {
    std::lock_guard<std::mutex> guard(g_lock);

    if (!g_initialized.load()) return;

    g_initialized.store(false);

    if (g_trackerHandle != ML_INVALID_HANDLE) {
        MLResult r = MLSpatialAnchorTrackerDestroy(g_trackerHandle);
        if (g_debug) {
            LOGI("MLSpatialAnchorTrackerDestroy r=%d (%s)", (int)r, ResultToString(r));
        }
        g_trackerHandle = ML_INVALID_HANDLE;
    }

    g_anchors.clear();
    g_autoCreate = false;

    LOGI("Spatial anchor tracker shutdown");
}