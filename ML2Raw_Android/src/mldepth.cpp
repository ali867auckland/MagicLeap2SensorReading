#include "mldepth.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

#include <android/log.h>
#include <ml_depth_camera.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MLDepthUnity", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MLDepthUnity", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "MLDepthUnity", __VA_ARGS__)

// ===== Global state =====
static std::atomic<bool> g_running{false};
static std::thread g_thread;

static MLHandle g_handle = ML_INVALID_HANDLE;
static std::mutex g_lock;

// Processed depth
static DepthFrameInfo g_depthInfo{};
static std::vector<uint8_t> g_depthBytes;

// Optional extras
static DepthFrameInfo g_confInfo{};
static std::vector<uint8_t> g_confBytes;

static DepthFrameInfo g_flagsInfo{};
static std::vector<uint8_t> g_flagsBytes;

static DepthFrameInfo g_rawInfo{};
static std::vector<uint8_t> g_rawBytes;

static DepthFrameInfo g_ambientRawInfo{};
static std::vector<uint8_t> g_ambientRawBytes;

static uint32_t g_flagsMask = 0;

// Stream constants - must match C# and SDK
static constexpr uint32_t STREAM_LONG  = 1u << 0;  // MLDepthCameraStream_LongRange
static constexpr uint32_t STREAM_SHORT = 1u << 1;  // MLDepthCameraStream_ShortRange

// Exposure limits (microseconds) from SDK docs
static constexpr uint32_t EXPOSURE_LONG_MIN = 250;
static constexpr uint32_t EXPOSURE_LONG_MAX = 2000;
static constexpr uint32_t EXPOSURE_LONG_DEFAULT = 1000;

static constexpr uint32_t EXPOSURE_SHORT_MIN = 50;
static constexpr uint32_t EXPOSURE_SHORT_MAX = 375;
static constexpr uint32_t EXPOSURE_SHORT_DEFAULT = 200;

// ---------- Capture loop ----------
static void CaptureLoop() {
    LOGI("Capture thread started");
    
    while (g_running.load()) {
        MLDepthCameraData data;
        MLDepthCameraDataInit(&data);

        MLResult r = MLDepthCameraGetLatestDepthData(g_handle, 500, &data);

        if (r == MLResult_Timeout) {
            continue;
        }
        
        if (r != MLResult_Ok) {
            static int errCount = 0;
            if (errCount++ < 10) {
                LOGE("MLDepthCameraGetLatestDepthData failed: r=%d", (int)r);
            }
            continue;
        }

        if (data.frame_count == 0 || data.frames == nullptr) {
            MLDepthCameraReleaseDepthData(g_handle, &data);
            continue;
        }

        MLDepthCameraFrame* frame = &data.frames[0];
        const int64_t ts = (int64_t)frame->frame_timestamp;

        {
            std::lock_guard<std::mutex> guard(g_lock);

            // Depth (processed)
            MLDepthCameraFrameBuffer* depth = frame->depth_image;
            if (depth && depth->data && depth->size > 0) {
                g_depthInfo.width         = (int32_t)depth->width;
                g_depthInfo.height        = (int32_t)depth->height;
                g_depthInfo.strideBytes   = (int32_t)depth->stride;
                g_depthInfo.captureTimeNs = ts;
                g_depthInfo.bytesPerPixel = (int32_t)depth->bytes_per_unit;
                g_depthInfo.format        = 0;

                g_depthBytes.resize(depth->size);
                std::memcpy(g_depthBytes.data(), depth->data, depth->size);
            }

            // Confidence
            if ((g_flagsMask & MLDepthCameraFlags_Confidence) &&
                frame->confidence && frame->confidence->data && frame->confidence->size > 0) {
                MLDepthCameraFrameBuffer* conf = frame->confidence;
                g_confInfo.width         = (int32_t)conf->width;
                g_confInfo.height        = (int32_t)conf->height;
                g_confInfo.strideBytes   = (int32_t)conf->stride;
                g_confInfo.captureTimeNs = ts;
                g_confInfo.bytesPerPixel = (int32_t)conf->bytes_per_unit;
                g_confBytes.resize(conf->size);
                std::memcpy(g_confBytes.data(), conf->data, conf->size);
            }

            // Depth flags
            if ((g_flagsMask & MLDepthCameraFlags_DepthFlags) &&
                frame->flags && frame->flags->data && frame->flags->size > 0) {
                MLDepthCameraFrameBuffer* fl = frame->flags;
                g_flagsInfo.width         = (int32_t)fl->width;
                g_flagsInfo.height        = (int32_t)fl->height;
                g_flagsInfo.strideBytes   = (int32_t)fl->stride;
                g_flagsInfo.captureTimeNs = ts;
                g_flagsInfo.bytesPerPixel = (int32_t)fl->bytes_per_unit;
                g_flagsBytes.resize(fl->size);
                std::memcpy(g_flagsBytes.data(), fl->data, fl->size);
            }

            // Raw depth
            if ((g_flagsMask & MLDepthCameraFlags_RawDepthImage) &&
                frame->raw_depth_image && frame->raw_depth_image->data && frame->raw_depth_image->size > 0) {
                MLDepthCameraFrameBuffer* raw = frame->raw_depth_image;
                g_rawInfo.width         = (int32_t)raw->width;
                g_rawInfo.height        = (int32_t)raw->height;
                g_rawInfo.strideBytes   = (int32_t)raw->stride;
                g_rawInfo.captureTimeNs = ts;
                g_rawInfo.bytesPerPixel = (int32_t)raw->bytes_per_unit;
                g_rawBytes.resize(raw->size);
                std::memcpy(g_rawBytes.data(), raw->data, raw->size);
            }

            // Ambient raw depth
            if ((g_flagsMask & MLDepthCameraFlags_AmbientRawDepthImage) &&
                frame->ambient_raw_depth_image && frame->ambient_raw_depth_image->data && frame->ambient_raw_depth_image->size > 0) {
                MLDepthCameraFrameBuffer* amb = frame->ambient_raw_depth_image;
                g_ambientRawInfo.width         = (int32_t)amb->width;
                g_ambientRawInfo.height        = (int32_t)amb->height;
                g_ambientRawInfo.strideBytes   = (int32_t)amb->stride;
                g_ambientRawInfo.captureTimeNs = ts;
                g_ambientRawInfo.bytesPerPixel = (int32_t)amb->bytes_per_unit;
                g_ambientRawBytes.resize(amb->size);
                std::memcpy(g_ambientRawBytes.data(), amb->data, amb->size);
            }
        }

        MLDepthCameraReleaseDepthData(g_handle, &data);
    }
    
    LOGI("Capture thread exiting");
}

// ---------- Init ----------
bool MLDepthUnity_Init(uint32_t streamMask, uint32_t flagsMask, uint32_t frameRateEnum) {
    if (g_running.load()) {
        LOGI("Already running");
        return true;
    }

    // Force single stream - SDK only supports one at a time
    if (streamMask == (STREAM_LONG | STREAM_SHORT)) {
        LOGW("Both streams requested - forcing SHORT only (SDK limitation)");
        streamMask = STREAM_SHORT;
    }
    
    // If no stream specified, default to SHORT
    if (streamMask == 0) {
        streamMask = STREAM_SHORT;
    }

    g_flagsMask = flagsMask;

    // Determine which stream we're using
    bool useShort = (streamMask & STREAM_SHORT) != 0;
    bool useLong = (streamMask & STREAM_LONG) != 0 && !useShort;

    // Pick appropriate frame rate
    MLDepthCameraFrameRate frameRate;
    uint32_t exposure;
    
    if (useShort) {
        // Short range: supports 5, 25, 50 (50Hz) or 5, 30, 60 (60Hz)
        // Use 5 FPS for reliability
        frameRate = MLDepthCameraFrameRate_5FPS;
        exposure = EXPOSURE_SHORT_DEFAULT;
        LOGI("Using SHORT range: exposure=%u fps=5", exposure);
    } else {
        // Long range: supports 1, 5 FPS only
        frameRate = MLDepthCameraFrameRate_5FPS;
        exposure = EXPOSURE_LONG_DEFAULT;
        LOGI("Using LONG range: exposure=%u fps=5", exposure);
    }

    // Start with minimal flags if too many requested
    uint32_t safeFlags = flagsMask;
    if (flagsMask > MLDepthCameraFlags_DepthImage) {
        // Keep depth, optionally confidence
        safeFlags = MLDepthCameraFlags_DepthImage;
        if (flagsMask & MLDepthCameraFlags_Confidence) {
            safeFlags |= MLDepthCameraFlags_Confidence;
        }
        LOGW("Reducing flags from %u to %u for stability", flagsMask, safeFlags);
    }
    g_flagsMask = safeFlags;

    MLDepthCameraSettings settings;
    MLDepthCameraSettingsInit(&settings);

    settings.streams = streamMask;

    // Configure the active stream
    if (useShort) {
        settings.stream_configs[MLDepthCameraFrameType_ShortRange].flags = safeFlags;
        settings.stream_configs[MLDepthCameraFrameType_ShortRange].exposure = exposure;
        settings.stream_configs[MLDepthCameraFrameType_ShortRange].frame_rate = frameRate;
    }
    if (useLong) {
        settings.stream_configs[MLDepthCameraFrameType_LongRange].flags = safeFlags;
        settings.stream_configs[MLDepthCameraFrameType_LongRange].exposure = exposure;
        settings.stream_configs[MLDepthCameraFrameType_LongRange].frame_rate = frameRate;
    }

    LOGI("Connecting: streams=%u flags=%u exposure=%u frameRate=%d", 
         streamMask, safeFlags, exposure, (int)frameRate);

    MLResult r = MLDepthCameraConnect(&settings, &g_handle);
    
    if (r != MLResult_Ok || g_handle == ML_INVALID_HANDLE) {
        LOGE("MLDepthCameraConnect FAILED r=%d", (int)r);
        LOGE("Check: com.magicleap.permission.DEPTH_CAMERA in manifest");
        g_handle = ML_INVALID_HANDLE;
        return false;
    }

    LOGI("MLDepthCameraConnect OK handle=%llu", (unsigned long long)g_handle);

    g_running.store(true);
    g_thread = std::thread(CaptureLoop);
    
    return true;
}

// ---------- Copy out helpers ----------
static bool CopyOut(const std::vector<uint8_t>& src, const DepthFrameInfo& info,
                    DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
    if (!outInfo || !outBytes || !written) return false;

    std::lock_guard<std::mutex> guard(g_lock);
    if (src.empty()) return false;

    int32_t n = (int32_t)src.size();
    if (n > cap) return false;

    *outInfo = info;
    std::memcpy(outBytes, src.data(), n);
    *written = n;
    return true;
}

bool MLDepthUnity_TryGetLatestDepth(uint32_t, DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
    return CopyOut(g_depthBytes, g_depthInfo, outInfo, outBytes, cap, written);
}

bool MLDepthUnity_TryGetLatestConfidence(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
    return CopyOut(g_confBytes, g_confInfo, outInfo, outBytes, cap, written);
}

bool MLDepthUnity_TryGetLatestDepthFlags(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
    return CopyOut(g_flagsBytes, g_flagsInfo, outInfo, outBytes, cap, written);
}

bool MLDepthUnity_TryGetLatestRawDepth(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
    return CopyOut(g_rawBytes, g_rawInfo, outInfo, outBytes, cap, written);
}

bool MLDepthUnity_TryGetLatestAmbientRawDepth(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
    return CopyOut(g_ambientRawBytes, g_ambientRawInfo, outInfo, outBytes, cap, written);
}

// ---------- Shutdown ----------
void MLDepthUnity_Shutdown() {
    LOGI("Shutting down...");
    
    g_running.store(false);
    
    if (g_thread.joinable()) {
        g_thread.join();
    }

    if (g_handle != ML_INVALID_HANDLE) {
        MLDepthCameraDisconnect(g_handle);
        g_handle = ML_INVALID_HANDLE;
    }

    std::lock_guard<std::mutex> guard(g_lock);
    g_depthBytes.clear();
    g_confBytes.clear();
    g_flagsBytes.clear();
    g_rawBytes.clear();
    g_ambientRawBytes.clear();

    g_depthInfo = DepthFrameInfo{};
    g_confInfo = DepthFrameInfo{};
    g_flagsInfo = DepthFrameInfo{};
    g_rawInfo = DepthFrameInfo{};
    g_ambientRawInfo = DepthFrameInfo{};
    
    LOGI("Shutdown complete");
}