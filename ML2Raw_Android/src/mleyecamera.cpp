#include "mleyecamera.h"

#include <atomic>
#include <mutex>
#include <map>
#include <cstring>

#include <android/log.h>
#include <ml_eye_camera.h>

#define LOG_TAG "MLEyeCameraUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

// Debug toggle
static bool g_debug = true;

// Global state
static std::mutex g_lock;
static std::atomic<bool> g_initialized{false};

static MLHandle g_eyeCameraHandle = ML_INVALID_HANDLE;
static uint32_t g_activeCamerasMask = 0;

// Per-camera frame tracking
struct CameraState {
    int64_t last_frame_number = -1;
    uint64_t total_frames = 0;
    bool has_new_frame = false;
    
    // Latest frame data (copied)
    EyeCameraFrameInfo info;
    std::vector<uint8_t> data;
};

static std::map<uint32_t, CameraState> g_cameraStates;

// Helper: MLResult to string
static const char* ResultToString(MLResult r) {
    switch (r) {
        case MLResult_Ok: return "Ok";
        case MLResult_InvalidParam: return "InvalidParam";
        case MLResult_UnspecifiedFailure: return "UnspecifiedFailure";
        case MLResult_PermissionDenied: return "PermissionDenied";
        case MLResult_Timeout: return "Timeout";
        default: return "Unknown";
    }
}

// Helper: Camera ID to name
static const char* CameraName(uint32_t id) {
    switch (id) {
        case 1: return "LeftTemple";
        case 2: return "LeftNasal";
        case 4: return "RightNasal";
        case 8: return "RightTemple";
        default: return "Unknown";
    }
}

// Initialize eye camera
bool MLEyeCameraUnity_Init(uint32_t camera_mask) {
    std::lock_guard<std::mutex> guard(g_lock);

    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }

    if (camera_mask == 0) {
        LOGE("No cameras specified in mask");
        return false;
    }

    g_activeCamerasMask = camera_mask;

    // Initialize camera states for enabled cameras
    const uint32_t all_cameras[] = {1, 2, 4, 8}; // LeftTemple, LeftNasal, RightNasal, RightTemple
    for (uint32_t cam_id : all_cameras) {
        if (camera_mask & cam_id) {
            g_cameraStates[cam_id] = CameraState();
            if (g_debug) {
                LOGI("Enabled camera: %s (id=%u)", CameraName(cam_id), cam_id);
            }
        }
    }

    // Setup eye camera settings
    MLEyeCameraSettings settings;
    MLEyeCameraSettingsInit(&settings);
    settings.cameras = camera_mask;

    // Connect to eye cameras
    MLResult r = MLEyeCameraConnect(&settings, &g_eyeCameraHandle);
    if (r != MLResult_Ok || g_eyeCameraHandle == ML_INVALID_HANDLE) {
        LOGE("MLEyeCameraConnect FAILED r=%d (%s)", (int)r, ResultToString(r));
        g_eyeCameraHandle = ML_INVALID_HANDLE;
        g_cameraStates.clear();
        return false;
    }

    if (g_debug) {
        LOGI("MLEyeCameraConnect OK handle=%llu mask=0x%X", 
             (unsigned long long)g_eyeCameraHandle, camera_mask);
    }

    g_initialized.store(true);
    LOGI("Eye camera initialized successfully");
    return true;
}

// Try to get latest frame from specific camera
bool MLEyeCameraUnity_TryGetLatestFrame(
    uint32_t camera_id,
    EyeCameraFrameInfo* out_info,
    uint8_t* out_bytes,
    int32_t capacity_bytes,
    int32_t* bytes_written)
{
    if (!out_info || !out_bytes || !bytes_written) return false;

    *bytes_written = 0;
    std::memset(out_info, 0, sizeof(EyeCameraFrameInfo));

    if (!g_initialized.load()) return false;

    std::lock_guard<std::mutex> guard(g_lock);

    // Check if this camera is enabled
    if (g_cameraStates.find(camera_id) == g_cameraStates.end()) {
        return false;
    }

    CameraState& cam = g_cameraStates[camera_id];

    // If no new frame, return cached data
    if (!cam.has_new_frame && cam.data.empty()) {
        return false;
    }

    // Return cached frame
    if (cam.data.size() > (size_t)capacity_bytes) {
        // Buffer too small - tell caller how much is needed
        *bytes_written = (int32_t)cam.data.size();
        return false;
    }

    // Copy cached data
    *out_info = cam.info;
    std::memcpy(out_bytes, cam.data.data(), cam.data.size());
    *bytes_written = (int32_t)cam.data.size();

    // Clear new frame flag (consumed)
    cam.has_new_frame = false;

    return true;
}

// Poll for new frames (should be called regularly from Update thread)
static void PollFrames() {
    if (!g_initialized.load()) return;

    // Lock is already held by caller

    // Get latest camera data (with timeout)
    MLEyeCameraData data;
    MLEyeCameraDataInit(&data);

    MLResult r = MLEyeCameraGetLatestCameraData(g_eyeCameraHandle, 10, &data);
    
    if (r == MLResult_Timeout) {
        // No new frames yet
        return;
    }

    if (r != MLResult_Ok) {
        if (g_debug && r != MLResult_Timeout) {
            LOGW("MLEyeCameraGetLatestCameraData r=%d (%s)", (int)r, ResultToString(r));
        }
        return;
    }

    if (data.frame_count == 0 || data.frames == nullptr) {
        MLEyeCameraReleaseCameraData(g_eyeCameraHandle, &data);
        return;
    }

    // Process each frame
    for (uint8_t i = 0; i < data.frame_count; i++) {
        MLEyeCameraFrame& frame = data.frames[i];
        uint32_t cam_id = (uint32_t)frame.camera_id;

        // Check if this is a camera we're tracking
        if (g_cameraStates.find(cam_id) == g_cameraStates.end()) {
            continue;
        }

        CameraState& cam = g_cameraStates[cam_id];

        // Check if this is a new frame
        if (frame.frame_number <= cam.last_frame_number) {
            continue; // Old frame, skip
        }

        // Update frame info
        cam.info.camera_id = cam_id;
        cam.info.frame_number = frame.frame_number;
        cam.info.timestamp_ns = (int64_t)frame.timestamp;
        cam.info.width = frame.frame_buffer.width;
        cam.info.height = frame.frame_buffer.height;
        cam.info.stride = frame.frame_buffer.stride;
        cam.info.bytes_per_pixel = frame.frame_buffer.bytes_per_pixel;
        cam.info.size = frame.frame_buffer.size;

        // Copy frame data
        if (frame.frame_buffer.data && frame.frame_buffer.size > 0) {
            cam.data.resize(frame.frame_buffer.size);
            std::memcpy(cam.data.data(), frame.frame_buffer.data, frame.frame_buffer.size);
        }

        // Update state
        cam.last_frame_number = frame.frame_number;
        cam.total_frames++;
        cam.has_new_frame = true;

        if (g_debug && (cam.total_frames % 30 == 0)) {
            LOGI("Camera %s: frame=%lld total=%llu size=%u %ux%u",
                 CameraName(cam_id),
                 (long long)frame.frame_number,
                 (unsigned long long)cam.total_frames,
                 frame.frame_buffer.size,
                 frame.frame_buffer.width,
                 frame.frame_buffer.height);
        }
    }

    // Release system memory
    MLEyeCameraReleaseCameraData(g_eyeCameraHandle, &data);
}

// Check if camera has new frame (triggers poll)
bool MLEyeCameraUnity_HasNewFrame(uint32_t camera_id) {
    if (!g_initialized.load()) return false;

    std::lock_guard<std::mutex> guard(g_lock);

    // Poll for new frames first
    PollFrames();

    // Check if this camera has new data
    if (g_cameraStates.find(camera_id) == g_cameraStates.end()) {
        return false;
    }

    return g_cameraStates[camera_id].has_new_frame;
}

// Get frame count for camera
uint64_t MLEyeCameraUnity_GetFrameCount(uint32_t camera_id) {
    if (!g_initialized.load()) return 0;

    std::lock_guard<std::mutex> guard(g_lock);

    if (g_cameraStates.find(camera_id) == g_cameraStates.end()) {
        return 0;
    }

    return g_cameraStates[camera_id].total_frames;
}

bool MLEyeCameraUnity_IsInitialized() {
    return g_initialized.load();
}

void MLEyeCameraUnity_Shutdown() {
    std::lock_guard<std::mutex> guard(g_lock);

    if (!g_initialized.load()) return;

    g_initialized.store(false);

    if (g_eyeCameraHandle != ML_INVALID_HANDLE) {
        MLResult r = MLEyeCameraDisconnect(g_eyeCameraHandle);
        if (g_debug) {
            LOGI("MLEyeCameraDisconnect r=%d (%s)", (int)r, ResultToString(r));
        }
        g_eyeCameraHandle = ML_INVALID_HANDLE;
    }

    g_cameraStates.clear();
    g_activeCamerasMask = 0;

    LOGI("Eye camera shutdown complete");
}