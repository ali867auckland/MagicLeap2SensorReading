#include "mleyetracking.h"
#include "mlperception_service.h"

#include <atomic>
#include <mutex>
#include <cstring>

#include <android/log.h>
#include <ml_eye_tracking.h>
#include <ml_head_tracking.h>
#include <ml_perception.h>

#define LOG_TAG "MLEyeTrackingUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

static bool g_debug = true;

static std::mutex g_mutex;
static std::atomic<bool> g_initialized{false};
static std::atomic<uint64_t> g_sampleCount{0};

static MLHandle g_eyeTracker = ML_INVALID_HANDLE;
static MLHandle g_headTracker = ML_INVALID_HANDLE;
static MLEyeTrackingStaticData g_staticData;

// Query pose from coordinate frame UID
static bool QueryPose(const MLCoordinateFrameUID& cfuid, 
                      float* out_pos_x, float* out_pos_y, float* out_pos_z,
                      float* out_rot_x, float* out_rot_y, float* out_rot_z) {
    
    MLSnapshot* snapshot = nullptr;
    MLResult r = MLPerceptionGetSnapshot(&snapshot);
    if (r != MLResult_Ok || !snapshot) {
        return false;
    }
    
    MLTransform transform;
    r = MLSnapshotGetTransform(snapshot, &cfuid, &transform);
    
    MLPerceptionReleaseSnapshot(snapshot);
    
    if (r != MLResult_Ok) {
        return false;
    }
    
    if (out_pos_x) *out_pos_x = transform.position.x;
    if (out_pos_y) *out_pos_y = transform.position.y;
    if (out_pos_z) *out_pos_z = transform.position.z;
    
    // Extract forward direction from quaternion (gaze is -Z in local space)
    // Forward = rotation * (0, 0, -1)
    float qx = transform.rotation.x;
    float qy = transform.rotation.y;
    float qz = transform.rotation.z;
    float qw = transform.rotation.w;
    
    // Rotate -Z by quaternion
    float fx = -2.0f * (qx * qz + qw * qy);
    float fy = -2.0f * (qy * qz - qw * qx);
    float fz = -(1.0f - 2.0f * (qx * qx + qy * qy));
    
    if (out_rot_x) *out_rot_x = fx;
    if (out_rot_y) *out_rot_y = fy;
    if (out_rot_z) *out_rot_z = fz;
    
    return true;
}

bool MLEyeTrackingUnity_Init(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }
    
    // Create eye tracker
    MLResult r = MLEyeTrackingCreate(&g_eyeTracker);
    if (r != MLResult_Ok) {
        LOGE("MLEyeTrackingCreate failed r=%d", (int)r);
        return false;
    }
    
    LOGI("Eye tracker created handle=%llu", (unsigned long long)g_eyeTracker);
    
    // Get static data (coordinate frame UIDs)
    r = MLEyeTrackingGetStaticData(g_eyeTracker, &g_staticData);
    if (r != MLResult_Ok) {
        LOGW("MLEyeTrackingGetStaticData failed r=%d (will retry)", (int)r);
    } else {
        LOGI("Got static data for vergence, left_center, right_center CFUIDs");
    }
    
    g_initialized.store(true);
    g_sampleCount.store(0);
    
    LOGI("Eye Tracking initialized");
    return true;
}

bool MLEyeTrackingUnity_GetLatest(EyeTrackingData* out_data) {
    if (!out_data) return false;
    if (!g_initialized.load()) return false;
    
    memset(out_data, 0, sizeof(EyeTrackingData));
    
    // Get eye tracking state
    MLEyeTrackingStateEx state;
    MLEyeTrackingStateInit(&state);
    
    MLResult r = MLEyeTrackingGetStateEx(g_eyeTracker, &state);
    if (r != MLResult_Ok) {
        if (g_debug) {
            static int errCount = 0;
            if (errCount++ < 5) {
                LOGW("MLEyeTrackingGetStateEx failed r=%d", (int)r);
            }
        }
        return false;
    }
    
    // Fill basic state data
    out_data->timestampNs = state.timestamp;
    out_data->vergence_confidence = state.vergence_confidence;
    out_data->left_center_confidence = state.left_center_confidence;
    out_data->right_center_confidence = state.right_center_confidence;
    out_data->left_blink = state.left_blink ? 1 : 0;
    out_data->right_blink = state.right_blink ? 1 : 0;
    out_data->left_eye_openness = state.left_eye_openness;
    out_data->right_eye_openness = state.right_eye_openness;
    out_data->error = (int32_t)state.error;
    
    // Query vergence pose
    out_data->vergence_valid = QueryPose(g_staticData.vergence,
        &out_data->vergence_x, &out_data->vergence_y, &out_data->vergence_z,
        nullptr, nullptr, nullptr) ? 1 : 0;
    
    // Query left eye center pose and gaze direction
    out_data->left_valid = QueryPose(g_staticData.left_center,
        &out_data->left_pos_x, &out_data->left_pos_y, &out_data->left_pos_z,
        &out_data->left_gaze_x, &out_data->left_gaze_y, &out_data->left_gaze_z) ? 1 : 0;
    
    // Query right eye center pose and gaze direction
    out_data->right_valid = QueryPose(g_staticData.right_center,
        &out_data->right_pos_x, &out_data->right_pos_y, &out_data->right_pos_z,
        &out_data->right_gaze_x, &out_data->right_gaze_y, &out_data->right_gaze_z) ? 1 : 0;
    
    g_sampleCount.fetch_add(1);
    
    return true;
}

bool MLEyeTrackingUnity_IsInitialized(void) {
    return g_initialized.load();
}

uint64_t MLEyeTrackingUnity_GetSampleCount(void) {
    return g_sampleCount.load();
}

void MLEyeTrackingUnity_Shutdown(void) {
    LOGI("Shutting down Eye Tracking...");
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_eyeTracker != ML_INVALID_HANDLE) {
        MLEyeTrackingDestroy(g_eyeTracker);
        g_eyeTracker = ML_INVALID_HANDLE;
    }
    
    g_initialized.store(false);
    g_sampleCount.store(0);
    
    LOGI("Eye Tracking shutdown complete");
}