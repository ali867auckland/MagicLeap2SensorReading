#include "mlworldcam.h"

#include <cstring>
#include <mutex>
#include <atomic>
#include <vector>
#include <thread>

#include <ml_world_camera.h>

#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MLWorldCamUnity", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MLWorldCamUnity", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "MLWorldCamUnity", __VA_ARGS__)

// Camera identifiers (must match SDK)
static constexpr uint32_t CAM_LEFT   = 1u << 0;  // MLWorldCameraIdentifier_Left
static constexpr uint32_t CAM_RIGHT  = 1u << 1;  // MLWorldCameraIdentifier_Right
static constexpr uint32_t CAM_CENTER = 1u << 2;  // MLWorldCameraIdentifier_Center
static constexpr uint32_t CAM_ALL    = CAM_LEFT | CAM_RIGHT | CAM_CENTER;

static std::mutex g_mutex;
static MLHandle g_handle = ML_INVALID_HANDLE;
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_running{false};

// Frame buffer
static WorldCamFrameInfo g_latestInfo;
static std::vector<uint8_t> g_latestBytes;
static std::atomic<bool> g_hasNewFrame{false};

// Capture thread
static std::thread g_thread;

static void CaptureLoop() {
    LOGI("Capture thread started");
    
    while (g_running.load()) {
        // MUST pre-initialize the struct - SDK checks this!
        MLWorldCameraData data;
        MLWorldCameraDataInit(&data);
        MLWorldCameraData* data_ptr = &data;
        
        MLResult r = MLWorldCameraGetLatestWorldCameraData(g_handle, 500, &data_ptr);
        
        if (r == MLResult_Timeout) {
            continue;
        }
        
        if (r != MLResult_Ok) {
            static int errCount = 0;
            if (errCount++ < 10) {
                LOGE("MLWorldCameraGetLatestWorldCameraData failed: r=%d", (int)r);
            }
            continue;
        }
        
        if (!data_ptr || data_ptr->frame_count == 0 || !data_ptr->frames) {
            if (data_ptr) {
                MLWorldCameraReleaseCameraData(g_handle, data_ptr);
            }
            continue;
        }
        
        // Get first available frame
        const MLWorldCameraFrame& f = data_ptr->frames[0];
        const MLWorldCameraFrameBuffer& fb = f.frame_buffer;
        
        if (fb.data && fb.size > 0) {
            std::lock_guard<std::mutex> lock(g_mutex);
            
            g_latestInfo.camId = (int32_t)f.id;
            g_latestInfo.frameType = (int32_t)f.frame_type;
            g_latestInfo.timestampNs = (int64_t)f.timestamp;
            g_latestInfo.width = (int32_t)fb.width;
            g_latestInfo.height = (int32_t)fb.height;
            g_latestInfo.strideBytes = (int32_t)fb.stride;
            g_latestInfo.bytesPerPixel = (int32_t)fb.bytes_per_pixel;
            
            g_latestBytes.resize(fb.size);
            std::memcpy(g_latestBytes.data(), fb.data, fb.size);
            
            g_hasNewFrame.store(true);
        }
        
        MLWorldCameraReleaseCameraData(g_handle, data_ptr);
    }
    
    LOGI("Capture thread exiting");
}

extern "C" bool MLWorldCamUnity_Init(uint32_t identifiers_mask) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }
    
    // If all cameras requested (7), try center first for compatibility
    uint32_t cameras = identifiers_mask;
    if (cameras == CAM_ALL || cameras == 0) {
        LOGW("All/no cameras requested, trying CENTER only for compatibility");
        cameras = CAM_CENTER;
    }
    
    MLWorldCameraSettings settings;
    MLWorldCameraSettingsInit(&settings);
    
    settings.cameras = cameras;
    settings.mode = MLWorldCameraMode_NormalExposure;  // Most compatible mode
    
    LOGI("Connecting: cameras=%u mode=%d", cameras, (int)settings.mode);
    
    MLResult r = MLWorldCameraConnect(&settings, &g_handle);
    
    if (r != MLResult_Ok || g_handle == ML_INVALID_HANDLE) {
        LOGE("MLWorldCameraConnect FAILED: r=%d", (int)r);
        
        // If center failed, try left
        if (cameras == CAM_CENTER) {
            LOGW("Center camera failed, trying LEFT...");
            settings.cameras = CAM_LEFT;
            r = MLWorldCameraConnect(&settings, &g_handle);
            
            if (r != MLResult_Ok || g_handle == ML_INVALID_HANDLE) {
                LOGE("LEFT camera also failed r=%d", (int)r);
                LOGE("Check: android.permission.CAMERA in manifest");
                LOGE("Check: No other app using world cameras");
                g_handle = ML_INVALID_HANDLE;
                return false;
            }
            LOGI("Connected to LEFT camera instead");
        } else {
            g_handle = ML_INVALID_HANDLE;
            return false;
        }
    }
    
    LOGI("MLWorldCameraConnect OK: handle=%llu", (unsigned long long)g_handle);
    
    g_initialized.store(true);
    g_running.store(true);
    g_thread = std::thread(CaptureLoop);
    
    LOGI("World camera initialized with capture thread");
    return true;
}

extern "C" bool MLWorldCamUnity_TryGetLatest(
    uint32_t timeout_ms,
    WorldCamFrameInfo* out_info,
    uint8_t* out_bytes,
    int32_t capacity_bytes,
    int32_t* out_bytes_written)
{
    (void)timeout_ms;
    
    if (!out_info || !out_bytes || capacity_bytes <= 0 || !out_bytes_written) {
        if (out_bytes_written) *out_bytes_written = 0;
        return false;
    }
    
    *out_bytes_written = 0;
    
    if (!g_initialized.load()) {
        return false;
    }
    
    if (!g_hasNewFrame.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_latestBytes.empty()) {
        return false;
    }
    
    int32_t required = (int32_t)g_latestBytes.size();
    if (required > capacity_bytes) {
        *out_bytes_written = required;
        return false;
    }
    
    *out_info = g_latestInfo;
    std::memcpy(out_bytes, g_latestBytes.data(), required);
    *out_bytes_written = required;
    
    g_hasNewFrame.store(false);
    
    return true;
}

extern "C" void MLWorldCamUnity_Shutdown() {
    LOGI("Shutting down...");
    
    g_running.store(false);
    
    if (g_thread.joinable()) {
        g_thread.join();
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_handle != ML_INVALID_HANDLE) {
        MLWorldCameraDisconnect(g_handle);
        g_handle = ML_INVALID_HANDLE;
    }
    
    g_latestBytes.clear();
    g_hasNewFrame.store(false);
    g_initialized.store(false);
    
    LOGI("Shutdown complete");
}