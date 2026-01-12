#include "mlheadtracking.h"

#include <atomic>
#include <mutex>
#include <cstring>

#include <android/log.h>
#include <ml_head_tracking.h>
#include <ml_perception.h>
#include <ml_snapshot.h>

#define LOG_TAG "MLHeadTrackingUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(__fmt__, ...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __fmt__, ##__VA_ARGS__)

// Debug toggle
static bool g_debug = true;

// Global state
static std::mutex g_lock;
static std::atomic<bool> g_initialized{false};

static MLHandle g_headTrackingHandle = ML_INVALID_HANDLE;
static MLCoordinateFrameUID g_headFrameUID = {{0}};

// Helper: MLResult to string
static const char* ResultToString(MLResult r) {
    switch (r) {
        case MLResult_Ok: return "Ok";
        case MLResult_InvalidParam: return "InvalidParam";
        case MLResult_UnspecifiedFailure: return "UnspecifiedFailure";
        case MLResult_PerceptionSystemNotStarted: return "PerceptionSystemNotStarted";
        case MLResult_Timeout: return "Timeout";
        default: return "Unknown";
    }
}

// Initialize head tracking
bool MLHeadTrackingUnity_Init() {
    std::lock_guard<std::mutex> guard(g_lock);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }

    // Create head tracking
    MLResult r = MLHeadTrackingCreate(&g_headTrackingHandle);
    if (r != MLResult_Ok || g_headTrackingHandle == ML_INVALID_HANDLE) {
        LOGE("MLHeadTrackingCreate FAILED r=%d (%s)", (int)r, ResultToString(r));
        g_headTrackingHandle = ML_INVALID_HANDLE;
        return false;
    }

    if (g_debug) {
        LOGI("MLHeadTrackingCreate OK handle=%llu", (unsigned long long)g_headTrackingHandle);
    }

    // Get the static data (coordinate frame UID)
    MLHeadTrackingStaticData staticData;
    r = MLHeadTrackingGetStaticData(g_headTrackingHandle, &staticData);
    if (r != MLResult_Ok) {
        LOGE("MLHeadTrackingGetStaticData FAILED r=%d (%s)", (int)r, ResultToString(r));
        MLHeadTrackingDestroy(g_headTrackingHandle);
        g_headTrackingHandle = ML_INVALID_HANDLE;
        return false;
    }

    g_headFrameUID = staticData.coord_frame_head;

    if (g_debug) {
        LOGI("Head tracking coordinate frame UID obtained");
    }

    g_initialized.store(true);
    LOGI("Head tracking initialized successfully");
    return true;
}

// Get current head pose
bool MLHeadTrackingUnity_GetPose(HeadPoseData* out_pose) {
    if (!out_pose) return false;

    // Zero out output
    std::memset(out_pose, 0, sizeof(HeadPoseData));
    out_pose->rotation_w = 1.0f; // Identity quaternion
    out_pose->confidence = 0.0f;

    if (!g_initialized.load()) {
        out_pose->resultCode = (int32_t)MLResult_PerceptionSystemNotStarted;
        return false;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    // Get perception snapshot
    MLSnapshot* snapshot = nullptr;
    MLResult r = MLPerceptionGetSnapshot(&snapshot);
    if (r != MLResult_Ok || snapshot == nullptr) {
        out_pose->resultCode = (int32_t)r;
        if (g_debug) {
            LOGW("MLPerceptionGetSnapshot failed r=%d", (int)r);
        }
        return false;
    }

    // Get head transform from snapshot
    MLTransform transform;
    std::memset(&transform, 0, sizeof(MLTransform));
    transform.rotation.w = 1.0f;

    r = MLSnapshotGetTransform(snapshot, &g_headFrameUID, &transform);
    
    // Get timestamp from snapshot
    MLTime snapshotTimestamp = 0;
    // Note: MLSnapshotGetTimestamp may not exist in all SDK versions
    // If it fails, we'll use 0

    // Release snapshot before processing results
    MLPerceptionReleaseSnapshot(snapshot);

    out_pose->resultCode = (int32_t)r;
    out_pose->timestampNs = (int64_t)snapshotTimestamp;

    if (r != MLResult_Ok) {
        if (g_debug && r != MLResult_Timeout) {
            LOGW("MLSnapshotGetTransform failed r=%d (%s)", (int)r, ResultToString(r));
        }
        return false;
    }

    // Copy transform
    out_pose->rotation_x = transform.rotation.x;
    out_pose->rotation_y = transform.rotation.y;
    out_pose->rotation_z = transform.rotation.z;
    out_pose->rotation_w = transform.rotation.w;
    out_pose->position_x = transform.position.x;
    out_pose->position_y = transform.position.y;
    out_pose->position_z = transform.position.z;

    // Get tracking state using the NEWER API (MLHeadTrackingStateEx)
    MLHeadTrackingStateEx stateEx;
    MLHeadTrackingStateExInit(&stateEx);
    
    r = MLHeadTrackingGetStateEx(g_headTrackingHandle, &stateEx);
    if (r == MLResult_Ok) {
        out_pose->status = (uint32_t)stateEx.status;
        out_pose->confidence = stateEx.confidence;
        out_pose->errorFlags = stateEx.error;  // This is a bitmask of MLHeadTrackingErrorFlag
    } else {
        // Fallback: try deprecated API
        MLHeadTrackingState state;
        state.mode = MLHeadTrackingMode_Unavailable;
        state.confidence = 0.0f;
        state.error = MLHeadTrackingError_Unknown;
        
        MLResult r2 = MLHeadTrackingGetState(g_headTrackingHandle, &state);
        if (r2 == MLResult_Ok) {
            // Map old mode to new status
            out_pose->status = (state.mode == MLHeadTrackingMode_6DOF) 
                ? (uint32_t)MLHeadTrackingStatus_Valid 
                : (uint32_t)MLHeadTrackingStatus_Invalid;
            out_pose->confidence = state.confidence;
            // Map old error enum to new error flags
            switch (state.error) {
                case MLHeadTrackingError_None:
                    out_pose->errorFlags = MLHeadTrackingErrorFlag_None;
                    break;
                case MLHeadTrackingError_NotEnoughFeatures:
                    out_pose->errorFlags = MLHeadTrackingErrorFlag_NotEnoughFeatures;
                    break;
                case MLHeadTrackingError_LowLight:
                    out_pose->errorFlags = MLHeadTrackingErrorFlag_LowLight;
                    break;
                default:
                    out_pose->errorFlags = MLHeadTrackingErrorFlag_Unknown;
                    break;
            }
        }
    }

    // Check for map events (returns bitmask, not array)
    uint64_t mapEventsMask = 0;
    r = MLHeadTrackingGetMapEvents(g_headTrackingHandle, &mapEventsMask);
    if (r == MLResult_Ok && mapEventsMask != 0) {
        out_pose->hasMapEvent = true;
        out_pose->mapEventsMask = mapEventsMask;
        
        if (g_debug) {
            LOGI("Map events bitmask: 0x%llx", (unsigned long long)mapEventsMask);
            if (mapEventsMask & MLHeadTrackingMapEvent_Lost) LOGI("  - Map Lost");
            if (mapEventsMask & MLHeadTrackingMapEvent_Recovered) LOGI("  - Map Recovered");
            if (mapEventsMask & MLHeadTrackingMapEvent_RecoveryFailed) LOGI("  - Recovery Failed");
            if (mapEventsMask & MLHeadTrackingMapEvent_NewSession) LOGI("  - New Session");
        }
    } else {
        out_pose->hasMapEvent = false;
        out_pose->mapEventsMask = 0;
    }

    return true;
}

// Get the handle for CV camera to use
uint64_t MLHeadTrackingUnity_GetHandle() {
    if (!g_initialized.load()) {
        return 0;
    }
    return (uint64_t)g_headTrackingHandle;
}

// Check if initialized
bool MLHeadTrackingUnity_IsInitialized() {
    return g_initialized.load();
}

// Shutdown
void MLHeadTrackingUnity_Shutdown() {
    std::lock_guard<std::mutex> guard(g_lock);

    if (!g_initialized.load()) {
        return;
    }

    g_initialized.store(false);

    if (g_headTrackingHandle != ML_INVALID_HANDLE) {
        MLResult r = MLHeadTrackingDestroy(g_headTrackingHandle);
        if (g_debug) {
            LOGI("MLHeadTrackingDestroy r=%d (%s)", (int)r, ResultToString(r));
        }
        g_headTrackingHandle = ML_INVALID_HANDLE;
    }

    g_headFrameUID = {{0}};

    LOGI("Head tracking shutdown complete");
}