#include "mlrgbcamera.h"
#include "mlcvcamera.h"  // Reuse existing CV camera functions

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <vector>

#include <android/log.h>
#include <ml_camera_v2.h>

#define LOG_TAG "MLRGBCameraUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

static bool g_debug = true;

// State
static std::mutex g_mutex;
static std::condition_variable g_cv;
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_capturing{false};
static std::atomic<uint64_t> g_frameCount{0};

// Camera handle
static MLCameraContext g_cameraContext = ML_INVALID_HANDLE;

// Frame buffer (single latest frame)
static RGBFrameWithPose g_latestFrameInfo;
static std::vector<uint8_t> g_latestFrameData;
static bool g_hasNewFrame = false;

// Camera config
static RGBCaptureMode g_captureMode = RGBCaptureMode_Video;

// Forward declarations
static void OnVideoBufferAvailable(const MLCameraOutput* output, const MLHandle metadata_handle, const MLCameraResultExtras* extra, void* data);
static void OnCaptureCompleted(const MLCameraResultExtras* extra, void* data);
static void OnCaptureFailed(const MLCameraResultExtras* extra, void* data);

// Get camera pose using existing CV camera infrastructure
static bool GetCameraPose(int64_t timestamp_ns, RGBFrameWithPose* out_info) {
    // Check if CV camera is initialized
    if (!MLCVCameraUnity_IsInitialized()) {
        out_info->pose_valid = 0;
        out_info->pose_result_code = -1;
        if (g_debug) {
            static int warnCount = 0;
            if (warnCount++ < 5) {
                LOGW("CV Camera not initialized - cannot get pose. Initialize CVCameraNativeConsumer first.");
            }
        }
        return false;
    }

    // Use the existing CV camera API to get pose
    CVCameraPose pose;
    bool ok = MLCVCameraUnity_GetPose(timestamp_ns, CVCameraID_ColorCamera, &pose);

    out_info->pose_result_code = pose.resultCode;

    if (ok && pose.resultCode == 0) {
        out_info->pose_rotation_x = pose.rotation_x;
        out_info->pose_rotation_y = pose.rotation_y;
        out_info->pose_rotation_z = pose.rotation_z;
        out_info->pose_rotation_w = pose.rotation_w;
        out_info->pose_position_x = pose.position_x;
        out_info->pose_position_y = pose.position_y;
        out_info->pose_position_z = pose.position_z;
        out_info->pose_valid = 1;
        return true;
    } else {
        out_info->pose_rotation_x = 0;
        out_info->pose_rotation_y = 0;
        out_info->pose_rotation_z = 0;
        out_info->pose_rotation_w = 1.0f;
        out_info->pose_position_x = 0;
        out_info->pose_position_y = 0;
        out_info->pose_position_z = 0;
        out_info->pose_valid = 0;
        return false;
    }
}

// Video buffer callback - called by ML camera system
static void OnVideoBufferAvailable(const MLCameraOutput* output, const MLHandle metadata_handle, const MLCameraResultExtras* extra, void* data) {
    (void)metadata_handle;
    (void)data;

    if (!output || output->plane_count == 0) {
        return;
    }

    // Get first plane
    const MLCameraPlaneInfo& plane = output->planes[0];
    
    if (!plane.data || plane.size == 0) {
        return;
    }

    int64_t timestamp_ns = (int64_t)extra->vcam_timestamp;

    // Build frame info
    RGBFrameWithPose info;
    memset(&info, 0, sizeof(info));
    
    info.width = (int32_t)plane.width;
    info.height = (int32_t)plane.height;
    info.strideBytes = (int32_t)plane.stride;
    info.format = (int32_t)output->format;
    info.timestampNs = timestamp_ns;

    // Get camera pose for this frame's timestamp using existing CV camera
    GetCameraPose(timestamp_ns, &info);

    // Intrinsics - set to 0, could be extracted from metadata if needed
    info.fx = info.fy = 0;
    info.cx = info.cy = 0;

    // Calculate total size for all planes
    size_t totalSize = 0;
    for (uint8_t i = 0; i < output->plane_count; i++) {
        totalSize += output->planes[i].size;
    }

    // Store frame
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        
        g_latestFrameInfo = info;
        
        g_latestFrameData.resize(totalSize);
        size_t offset = 0;
        for (uint8_t i = 0; i < output->plane_count; i++) {
            memcpy(g_latestFrameData.data() + offset, output->planes[i].data, output->planes[i].size);
            offset += output->planes[i].size;
        }
        
        g_hasNewFrame = true;
    }
    
    g_cv.notify_one();
    g_frameCount.fetch_add(1);

    if (g_debug && (g_frameCount.load() % 30 == 0)) {
        LOGI("Frame %llu: %dx%d ts=%lld pose_valid=%d (r=%d)", 
             (unsigned long long)g_frameCount.load(),
             info.width, info.height, 
             (long long)timestamp_ns,
             info.pose_valid,
             info.pose_result_code);
    }
}

static void OnCaptureCompleted(const MLCameraResultExtras* extra, void* data) {
    (void)extra;
    (void)data;
}

static void OnCaptureFailed(const MLCameraResultExtras* extra, void* data) {
    (void)extra;
    (void)data;
    LOGE("Capture failed");
}

static void OnDeviceAvailable(void* data) {
    (void)data;
    LOGI("Camera device available");
}

static void OnDeviceUnavailable(void* data) {
    (void)data;
    LOGW("Camera device unavailable");
}

// Initialize RGB camera (CV camera must be initialized separately)
bool MLRGBCameraUnity_Init(RGBCaptureMode mode) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }

    g_captureMode = mode;

    // Warn if CV camera not ready (but don't fail - it might be initialized later)
    if (!MLCVCameraUnity_IsInitialized()) {
        LOGW("CV Camera not yet initialized. Poses will not be available until CVCameraNativeConsumer starts.");
    }

    // Connect to RGB camera
    MLCameraConnectContext connectContext;
    MLCameraConnectContextInit(&connectContext);
    connectContext.cam_id = MLCameraIdentifier_MAIN;
    connectContext.flags = MLCameraConnectFlag_CamOnly;

    MLResult r = MLCameraConnect(&connectContext, &g_cameraContext);
    if (r != MLResult_Ok) {
        LOGE("MLCameraConnect failed r=%d", (int)r);
        return false;
    }

    if (g_debug) {
        LOGI("RGB Camera connected context=%llu", (unsigned long long)g_cameraContext);
    }

    // Set device status callbacks
    MLCameraDeviceStatusCallbacks deviceCallbacks;
    MLCameraDeviceStatusCallbacksInit(&deviceCallbacks);
    deviceCallbacks.on_device_available = OnDeviceAvailable;
    deviceCallbacks.on_device_unavailable = OnDeviceUnavailable;
    
    r = MLCameraSetDeviceStatusCallbacks(g_cameraContext, &deviceCallbacks, nullptr);
    if (r != MLResult_Ok) {
        LOGW("MLCameraSetDeviceStatusCallbacks failed r=%d", (int)r);
    }

    g_initialized.store(true);
    g_frameCount.store(0);
    
    LOGI("RGB Camera initialized (mode=%d)", (int)mode);
    return true;
}

// Start capture
bool MLRGBCameraUnity_StartCapture() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized.load()) {
        LOGE("Not initialized");
        return false;
    }

    if (g_capturing.load()) {
        LOGI("Already capturing");
        return true;
    }

    // Prepare capture config
    MLCameraCaptureConfig captureConfig;
    MLCameraCaptureConfigInit(&captureConfig);
    
    captureConfig.capture_frame_rate = MLCameraCaptureFrameRate_30FPS;
    captureConfig.num_streams = 1;
    
    // Configure stream based on mode
    MLCameraCaptureStreamConfig& streamConfig = captureConfig.stream_configs[0];
    streamConfig.capture_type = MLCameraCaptureType_Video;
    streamConfig.width = 1280;
    streamConfig.height = 720;
    streamConfig.output_format = MLCameraOutputFormat_YUV_420_888;
    
    if (g_captureMode == RGBCaptureMode_Preview) {
        streamConfig.width = 640;
        streamConfig.height = 480;
    } else if (g_captureMode == RGBCaptureMode_Image) {
        streamConfig.capture_type = MLCameraCaptureType_Image;
        streamConfig.width = 1920;
        streamConfig.height = 1080;
        streamConfig.output_format = MLCameraOutputFormat_JPEG;
    }

    MLResult r = MLCameraPrepareCapture(g_cameraContext, &captureConfig, nullptr);
    if (r != MLResult_Ok) {
        LOGE("MLCameraPrepareCapture failed r=%d", (int)r);
        return false;
    }

    // Set capture callbacks
    MLCameraCaptureCallbacks captureCallbacks;
    MLCameraCaptureCallbacksInit(&captureCallbacks);
    captureCallbacks.on_video_buffer_available = OnVideoBufferAvailable;
    captureCallbacks.on_capture_completed = OnCaptureCompleted;
    captureCallbacks.on_capture_failed = OnCaptureFailed;

    r = MLCameraSetCaptureCallbacks(g_cameraContext, &captureCallbacks, nullptr);
    if (r != MLResult_Ok) {
        LOGE("MLCameraSetCaptureCallbacks failed r=%d", (int)r);
        return false;
    }

    // Start video capture
    r = MLCameraCaptureVideoStart(g_cameraContext);
    if (r != MLResult_Ok) {
        LOGE("MLCameraCaptureVideoStart failed r=%d", (int)r);
        return false;
    }

    g_capturing.store(true);
    LOGI("RGB Camera capture started");
    return true;
}

// Stop capture
void MLRGBCameraUnity_StopCapture() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_capturing.load()) {
        return;
    }

    MLCameraCaptureVideoStop(g_cameraContext);
    g_capturing.store(false);
    
    LOGI("RGB Camera capture stopped");
}

// Try to get latest frame
bool MLRGBCameraUnity_TryGetLatestFrame(
    uint32_t timeout_ms,
    RGBFrameWithPose* out_info,
    uint8_t* out_bytes,
    int32_t capacity_bytes,
    int32_t* out_bytes_written
) {
    if (!out_info || !out_bytes_written) {
        return false;
    }

    *out_bytes_written = 0;

    std::unique_lock<std::mutex> lock(g_mutex);

    // Wait for frame if needed
    if (!g_hasNewFrame && timeout_ms > 0) {
        g_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), []{ return g_hasNewFrame; });
    }

    if (!g_hasNewFrame) {
        return false;
    }

    // Check capacity
    int32_t required = (int32_t)g_latestFrameData.size();
    if (required > capacity_bytes) {
        *out_bytes_written = required;
        return false;
    }

    // Copy frame
    *out_info = g_latestFrameInfo;
    memcpy(out_bytes, g_latestFrameData.data(), required);
    *out_bytes_written = required;
    
    g_hasNewFrame = false;
    
    return true;
}

uint64_t MLRGBCameraUnity_GetFrameCount() {
    return g_frameCount.load();
}

bool MLRGBCameraUnity_IsCapturing() {
    return g_capturing.load();
}

// Shutdown
void MLRGBCameraUnity_Shutdown() {
    LOGI("Shutting down RGB Camera...");

    MLRGBCameraUnity_StopCapture();

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_cameraContext != ML_INVALID_HANDLE) {
        MLCameraDisconnect(g_cameraContext);
        g_cameraContext = ML_INVALID_HANDLE;
    }

    // NOTE: We do NOT shutdown CV camera here - it's managed separately

    g_latestFrameData.clear();
    g_hasNewFrame = false;
    g_initialized.store(false);
    g_frameCount.store(0);

    LOGI("RGB Camera shutdown complete");
}