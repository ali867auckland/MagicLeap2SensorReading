#include "mlcvcamera.h"

#include <atomic>
#include <mutex>
#include <cstring>
#include <time.h>

#include <android/log.h>
#include <ml_cv_camera.h>
#include <ml_time.h>

#define LOG_TAG "MLCVCameraUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

// Debug toggle
static bool g_debug = true;

// Global state
static std::mutex g_lock;
static std::atomic<bool> g_initialized{false};

static MLHandle g_cvCameraHandle = ML_INVALID_HANDLE;
static MLHandle g_headTrackingHandle = ML_INVALID_HANDLE; // Borrowed, not owned

// Helper: MLResult to string
static const char* ResultToString(MLResult r) {
    switch (r) {
        case MLResult_Ok: return "Ok";
        case MLResult_InvalidParam: return "InvalidParam";
        case MLResult_UnspecifiedFailure: return "UnspecifiedFailure";
        case MLResult_PerceptionSystemNotStarted: return "PerceptionSystemNotStarted";
        case MLResult_PermissionDenied: return "PermissionDenied";
        case MLResult_Timeout: return "Timeout";
        default: return "Unknown";
    }
}

// Initialize with externally provided head tracking handle
bool MLCVCameraUnity_Init(uint64_t head_tracking_handle) {
    std::lock_guard<std::mutex> guard(g_lock);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }

    // Validate head tracking handle
    if (head_tracking_handle == 0 || head_tracking_handle == (uint64_t)ML_INVALID_HANDLE) {
        LOGE("MLCVCameraUnity_Init: Invalid head_tracking_handle. Initialize head tracking first!");
        return false;
    }

    g_headTrackingHandle = (MLHandle)head_tracking_handle;

    if (g_debug) {
        LOGI("Using head tracking handle: %llu", (unsigned long long)g_headTrackingHandle);
    }

    // Create CV camera tracker
    MLResult r = MLCVCameraTrackingCreate(&g_cvCameraHandle);
    if (r != MLResult_Ok || g_cvCameraHandle == ML_INVALID_HANDLE) {
        LOGE("MLCVCameraTrackingCreate FAILED r=%d (%s)", (int)r, ResultToString(r));
        g_cvCameraHandle = ML_INVALID_HANDLE;
        g_headTrackingHandle = ML_INVALID_HANDLE;
        return false;
    }

    if (g_debug) {
        LOGI("MLCVCameraTrackingCreate OK handle=%llu", (unsigned long long)g_cvCameraHandle);
    }

    g_initialized.store(true);
    LOGI("CV Camera tracking initialized successfully");
    return true;
}

// Get current time
int64_t MLCVCameraUnity_GetCurrentTimeNs() {
    MLTime mlTime = 0;
    MLResult r = MLTimeConvertSystemTimeToMLTime(0, &mlTime);
    
    if (r != MLResult_Ok) {
        // Fallback: use system clock
        struct timespec ts;
        clock_gettime(CLOCK_BOOTTIME, &ts);
        return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    }
    
    return (int64_t)mlTime;
}

// Get camera pose
bool MLCVCameraUnity_GetPose(int64_t timestamp_ns, CVCameraID camera_id, CVCameraPose* out_pose) {
    if (!out_pose) return false;
    
    // Zero out output
    std::memset(out_pose, 0, sizeof(CVCameraPose));
    out_pose->rotation_w = 1.0f; // Identity quaternion
    
    if (!g_initialized.load()) {
        out_pose->resultCode = (int32_t)MLResult_PerceptionSystemNotStarted;
        return false;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    // Use provided timestamp or get current time
    MLTime mlTimestamp = (MLTime)timestamp_ns;
    if (mlTimestamp == 0) {
        mlTimestamp = (MLTime)MLCVCameraUnity_GetCurrentTimeNs();
    }

    MLTransform transform;
    std::memset(&transform, 0, sizeof(MLTransform));
    transform.rotation.w = 1.0f;

    MLCVCameraID mlCameraId = MLCVCameraID_ColorCamera;

    MLResult r = MLCVCameraGetFramePose(
        g_cvCameraHandle,
        g_headTrackingHandle,
        mlCameraId,
        mlTimestamp,
        &transform
    );

    out_pose->resultCode = (int32_t)r;
    out_pose->timestampNs = (int64_t)mlTimestamp;

    if (r != MLResult_Ok) {
        if (g_debug && r != MLResult_Timeout) {
            LOGW("MLCVCameraGetFramePose r=%d (%s) ts=%lld",
                 (int)r, ResultToString(r), (long long)mlTimestamp);
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

    return true;
}

// Check if initialized
bool MLCVCameraUnity_IsInitialized() {
    return g_initialized.load();
}

// Shutdown (only CV camera, not head tracking)
void MLCVCameraUnity_Shutdown() {
    std::lock_guard<std::mutex> guard(g_lock);

    if (!g_initialized.load()) {
        return;
    }

    g_initialized.store(false);

    // Only destroy CV camera tracker (head tracking is owned elsewhere)
    if (g_cvCameraHandle != ML_INVALID_HANDLE) {
        MLResult r = MLCVCameraTrackingDestroy(g_cvCameraHandle);
        if (g_debug) {
            LOGI("MLCVCameraTrackingDestroy r=%d (%s)", (int)r, ResultToString(r));
        }
        g_cvCameraHandle = ML_INVALID_HANDLE;
    }

    // Clear borrowed handle (don't destroy)
    g_headTrackingHandle = ML_INVALID_HANDLE;

    LOGI("CV Camera tracking shutdown complete");
}