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

// Frame buffers for each camera (indexed by camera ID bit position)
// Index 0 = LEFT (bit 0), Index 1 = RIGHT (bit 1), Index 2 = CENTER (bit 2)
static WorldCamFrameInfo g_frameInfo[3];
static std::vector<uint8_t> g_frameBytes[3];
static std::atomic<bool> g_hasNewFrame[3] = {false, false, false};

// Track which cameras are enabled
static uint32_t g_enabledCameras = 0;

// Capture thread
static std::thread g_thread;

// Convert camera ID bitmask to array index
static int CamIdToIndex(uint32_t camId) {
    if (camId == CAM_LEFT) return 0;
    if (camId == CAM_RIGHT) return 1;
    if (camId == CAM_CENTER) return 2;
    return -1;
}

static const char* CamIdToName(uint32_t camId) {
    if (camId == CAM_LEFT) return "LEFT";
    if (camId == CAM_RIGHT) return "RIGHT";
    if (camId == CAM_CENTER) return "CENTER";
    return "UNKNOWN";
}

static void CaptureLoop() {
    LOGI("Capture thread started (enabled cameras mask=%u)", g_enabledCameras);
    
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
        
        // Process ALL frames returned (up to 3 cameras)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            
            for (uint8_t i = 0; i < data_ptr->frame_count; i++) {
                const MLWorldCameraFrame& f = data_ptr->frames[i];
                const MLWorldCameraFrameBuffer& fb = f.frame_buffer;
                
                int idx = CamIdToIndex((uint32_t)f.id);
                if (idx < 0 || idx >= 3) continue;
                
                if (fb.data && fb.size > 0) {
                    g_frameInfo[idx].camId = (int32_t)f.id;
                    g_frameInfo[idx].frameType = (int32_t)f.frame_type;
                    g_frameInfo[idx].timestampNs = (int64_t)f.timestamp;
                    g_frameInfo[idx].width = (int32_t)fb.width;
                    g_frameInfo[idx].height = (int32_t)fb.height;
                    g_frameInfo[idx].strideBytes = (int32_t)fb.stride;
                    g_frameInfo[idx].bytesPerPixel = (int32_t)fb.bytes_per_pixel;
                    
                    g_frameBytes[idx].resize(fb.size);
                    std::memcpy(g_frameBytes[idx].data(), fb.data, fb.size);
                    
                    g_hasNewFrame[idx].store(true);
                }
            }
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
    
    // Default to all cameras if none specified
    uint32_t cameras = identifiers_mask;
    if (cameras == 0) {
        cameras = CAM_ALL;
    }
    
    g_enabledCameras = cameras;
    
    MLWorldCameraSettings settings;
    MLWorldCameraSettingsInit(&settings);
    
    settings.cameras = cameras;
    settings.mode = MLWorldCameraMode_NormalExposure;
    
    LOGI("Connecting: cameras=%u (L=%d R=%d C=%d) mode=%d", 
         cameras,
         (cameras & CAM_LEFT) ? 1 : 0,
         (cameras & CAM_RIGHT) ? 1 : 0,
         (cameras & CAM_CENTER) ? 1 : 0,
         (int)settings.mode);
    
    MLResult r = MLWorldCameraConnect(&settings, &g_handle);
    
    if (r != MLResult_Ok || g_handle == ML_INVALID_HANDLE) {
        LOGE("MLWorldCameraConnect FAILED: r=%d", (int)r);
        LOGE("Check: android.permission.CAMERA in manifest");
        LOGE("Check: No other app using world cameras");
        g_handle = ML_INVALID_HANDLE;
        return false;
    }
    
    LOGI("MLWorldCameraConnect OK: handle=%llu", (unsigned long long)g_handle);
    
    // Clear all frame buffers
    for (int i = 0; i < 3; i++) {
        g_frameBytes[i].clear();
        g_hasNewFrame[i].store(false);
        std::memset(&g_frameInfo[i], 0, sizeof(WorldCamFrameInfo));
    }
    
    g_initialized.store(true);
    g_running.store(true);
    g_thread = std::thread(CaptureLoop);
    
    LOGI("World camera initialized with capture thread");
    return true;
}

// Get frame from specific camera (camId = 1=LEFT, 2=RIGHT, 4=CENTER)
extern "C" bool MLWorldCamUnity_TryGetLatest(
    uint32_t camId,
    WorldCamFrameInfo* out_info,
    uint8_t* out_bytes,
    int32_t capacity_bytes,
    int32_t* out_bytes_written)
{
    if (!out_info || !out_bytes || capacity_bytes <= 0 || !out_bytes_written) {
        if (out_bytes_written) *out_bytes_written = 0;
        return false;
    }
    
    *out_bytes_written = 0;
    
    if (!g_initialized.load()) {
        return false;
    }
    
    // Convert camId to index
    int idx = CamIdToIndex(camId);
    if (idx < 0 || idx >= 3) {
        return false;
    }
    
    if (!g_hasNewFrame[idx].load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_frameBytes[idx].empty()) {
        return false;
    }
    
    int32_t required = (int32_t)g_frameBytes[idx].size();
    if (required > capacity_bytes) {
        *out_bytes_written = required;
        return false;
    }
    
    *out_info = g_frameInfo[idx];
    std::memcpy(out_bytes, g_frameBytes[idx].data(), required);
    *out_bytes_written = required;
    
    g_hasNewFrame[idx].store(false);
    
    return true;
}

// Get count of cameras with new frames available
extern "C" int32_t MLWorldCamUnity_GetAvailableCount() {
    int32_t count = 0;
    for (int i = 0; i < 3; i++) {
        if (g_hasNewFrame[i].load()) count++;
    }
    return count;
}

// Check if specific camera has new frame
extern "C" bool MLWorldCamUnity_HasNewFrame(uint32_t camId) {
    int idx = CamIdToIndex(camId);
    if (idx < 0 || idx >= 3) return false;
    return g_hasNewFrame[idx].load();
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
    
    for (int i = 0; i < 3; i++) {
        g_frameBytes[i].clear();
        g_hasNewFrame[i].store(false);
    }
    
    g_enabledCameras = 0;
    g_initialized.store(false);
    
    LOGI("Shutdown complete");
}