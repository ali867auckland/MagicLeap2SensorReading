#include "mlgazerecognition.h"
#include "mlperception_service.h"

#include <atomic>
#include <mutex>
#include <cstring>
#include <cmath>

#include <android/log.h>
#include <ml_gaze_recognition.h>

#define LOG_TAG "MLGazeRecognitionUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

static bool g_debug = true;

static std::mutex g_mutex;
static std::atomic<bool> g_initialized{false};
static std::atomic<uint64_t> g_sampleCount{0};

static MLHandle g_gazeRecognition = ML_INVALID_HANDLE;

bool MLGazeRecognitionUnity_Init(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }
    
    // Create gaze recognition
    MLResult r = MLGazeRecognitionCreate(&g_gazeRecognition);
    if (r != MLResult_Ok) {
        LOGE("MLGazeRecognitionCreate failed r=%d", (int)r);
        return false;
    }
    
    LOGI("Gaze Recognition created handle=%llu", (unsigned long long)g_gazeRecognition);
    
    // Get static data (optional, for eye height/width max)
    MLGazeRecognitionStaticData staticData;
    MLGazeRecognitionStaticDataInit(&staticData);
    r = MLGazeRecognitionGetStaticData(g_gazeRecognition, &staticData);
    if (r == MLResult_Ok) {
        LOGI("Eye height max=%.2f, width max=%.2f", staticData.eye_height_max, staticData.eye_width_max);
    }
    
    g_initialized.store(true);
    g_sampleCount.store(0);
    
    LOGI("Gaze Recognition initialized");
    return true;
}

bool MLGazeRecognitionUnity_GetLatest(GazeRecognitionData* out_data) {
    if (!out_data) return false;
    if (!g_initialized.load()) return false;
    
    memset(out_data, 0, sizeof(GazeRecognitionData));
    
    // Get gaze recognition state
    MLGazeRecognitionState state;
    MLGazeRecognitionStateInit(&state);
    
    MLResult r = MLGazeRecognitionGetState(g_gazeRecognition, &state);
    if (r != MLResult_Ok) {
        if (g_debug) {
            static int errCount = 0;
            if (errCount++ < 5) {
                LOGW("MLGazeRecognitionGetState failed r=%d", (int)r);
            }
        }
        return false;
    }
    
    // Fill data
    out_data->timestampNs = state.timestamp;
    out_data->behavior = (int32_t)state.behavior;
    out_data->eye_left_x = state.eye_left.x;
    out_data->eye_left_y = state.eye_left.y;
    out_data->eye_right_x = state.eye_right.x;
    out_data->eye_right_y = state.eye_right.y;
    out_data->onset_s = state.onset_s;
    out_data->duration_s = state.duration_s;
    out_data->velocity_degps = state.velocity_degps;
    out_data->amplitude_deg = state.amplitude_deg;
    out_data->direction_radial = state.direction_radial;
    out_data->error = (int32_t)state.error;
    
    g_sampleCount.fetch_add(1);
    
    return true;
}

bool MLGazeRecognitionUnity_IsInitialized(void) {
    return g_initialized.load();
}

uint64_t MLGazeRecognitionUnity_GetSampleCount(void) {
    return g_sampleCount.load();
}

void MLGazeRecognitionUnity_Shutdown(void) {
    LOGI("Shutting down Gaze Recognition...");
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_gazeRecognition != ML_INVALID_HANDLE) {
        MLGazeRecognitionDestroy(g_gazeRecognition);
        g_gazeRecognition = ML_INVALID_HANDLE;
    }
    
    g_initialized.store(false);
    g_sampleCount.store(0);
    
    LOGI("Gaze Recognition shutdown complete");
}