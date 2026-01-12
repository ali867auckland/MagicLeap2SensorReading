#include "mldepth_unity.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

#include <android/log.h>
#include <ml_depth_camera.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MLDepthUnity", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MLDepthUnity", __VA_ARGS__)

// ===== Optional Debug Toggles =====
static bool g_debug = true;              // verbose logs
static bool g_rejectBothStreams = true;  // hard fail if both streams requested

// ===== Global state =====
static std::atomic<bool> g_running{false};
static std::thread g_thread;

static MLHandle g_handle = ML_INVALID_HANDLE;
static std::mutex g_lock;

// Processed depth
static DepthFrameInfo g_depthInfo{};
static std::vector<uint8_t> g_depthBytes;

// Optional extras (FIXED: separate infos for metadata correctness)
static DepthFrameInfo g_confInfo{};
static std::vector<uint8_t> g_confBytes;

static DepthFrameInfo g_flagsInfo{};
static std::vector<uint8_t> g_flagsBytes;

// Raw depth
static DepthFrameInfo g_rawInfo{};
static std::vector<uint8_t> g_rawBytes;

// Ambient raw depth
static DepthFrameInfo g_ambientRawInfo{};
static std::vector<uint8_t> g_ambientRawBytes;

static uint32_t g_streamMask = 0;
static uint32_t g_flagsMask  = 0;
static uint32_t g_frameRate  = 0;

// These MUST match your C# bit assignments
static constexpr uint32_t STREAM_LONG  = 1u << 0;
static constexpr uint32_t STREAM_SHORT = 1u << 1;

// FrameRate enum values (per your mapping)
static constexpr uint32_t FPS_1_ENUM  = 0;
static constexpr uint32_t FPS_5_ENUM  = 1;
static constexpr uint32_t FPS_25_ENUM = 2;

// ---------- Safety helpers ----------
static inline bool HasLong(uint32_t mask)  { return (mask & STREAM_LONG) != 0; }
static inline bool HasShort(uint32_t mask) { return (mask & STREAM_SHORT) != 0; }

static uint32_t PickSafeFps(uint32_t streamMask, uint32_t requestedFpsEnum) {
  // Long range only supports 1 or 5 FPS → force 5
  if (streamMask == STREAM_LONG) return FPS_5_ENUM;

  // Short range supports 25 FPS → force 25
  if (streamMask == STREAM_SHORT) return FPS_25_ENUM;

  // If both requested (normally unsupported), choose short->25 as default
  if (HasShort(streamMask)) return FPS_25_ENUM;

  return requestedFpsEnum;
}

static const char* StreamMaskToStr(uint32_t mask) {
  if (mask == STREAM_LONG)  return "LONG";
  if (mask == STREAM_SHORT) return "SHORT";
  if (mask == (STREAM_LONG | STREAM_SHORT)) return "LONG|SHORT";
  return "UNKNOWN";
}

static void LogSettings(const MLDepthCameraSettings& settings, uint32_t streamMask) {
  if (!g_debug) return;

  LOGI("==== MLDepthUnity Settings Dump ====");
  LOGI("settings.streams=%u (%s)", (unsigned)settings.streams, StreamMaskToStr(settings.streams));

  if (HasLong(streamMask)) {
    const auto& cfg = settings.stream_configs[MLDepthCameraFrameType_LongRange];
    LOGI("[LONG] flags=%u frame_rate=%d", (unsigned)cfg.flags, (int)cfg.frame_rate);
  }
  if (HasShort(streamMask)) {
    const auto& cfg = settings.stream_configs[MLDepthCameraFrameType_ShortRange];
    LOGI("[SHORT] flags=%u frame_rate=%d", (unsigned)cfg.flags, (int)cfg.frame_rate);
  }
  LOGI("====================================");
}

// ---------- Capture loop ----------
static void CaptureLoop() {
  while (g_running.load()) {
    MLDepthCameraData data;
    MLDepthCameraDataInit(&data);

    MLResult r = MLDepthCameraGetLatestDepthData(g_handle, /*timeout_ms*/ 200, &data);

    if (r == MLResult_Timeout) continue;
    if (r != MLResult_Ok) continue;

    if (data.frame_count == 0 || data.frames == nullptr) {
      MLDepthCameraReleaseDepthData(g_handle, &data);
      continue;
    }

    // Take the first frame returned.
    MLDepthCameraFrame* frame = &data.frames[0];

    // Processed depth
    MLDepthCameraFrameBuffer* depth = frame->depth_image;

    {
      std::lock_guard<std::mutex> guard(g_lock);

      const int64_t ts = (int64_t)frame->frame_timestamp;

      // Depth (processed)
      if (depth && depth->data && depth->size > 0) {
        g_depthInfo.width         = (int32_t)depth->width;
        g_depthInfo.height        = (int32_t)depth->height;
        g_depthInfo.strideBytes   = (int32_t)depth->stride;
        g_depthInfo.captureTimeNs = ts;
        g_depthInfo.bytesPerPixel = (int32_t)depth->bytes_per_unit;
        g_depthInfo.format        = 0;

        g_depthBytes.resize(depth->size);
        std::memcpy(g_depthBytes.data(), depth->data, depth->size);
      } else {
        g_depthBytes.clear();
      }

      // Confidence (FIXED: store correct metadata)
      if ((g_flagsMask & MLDepthCameraFlags_Confidence) &&
          frame->confidence && frame->confidence->data && frame->confidence->size > 0) {
        MLDepthCameraFrameBuffer* conf = frame->confidence;

        g_confInfo.width         = (int32_t)conf->width;
        g_confInfo.height        = (int32_t)conf->height;
        g_confInfo.strideBytes   = (int32_t)conf->stride;
        g_confInfo.captureTimeNs = ts;
        g_confInfo.bytesPerPixel = (int32_t)conf->bytes_per_unit;
        g_confInfo.format        = 0;

        g_confBytes.resize(conf->size);
        std::memcpy(g_confBytes.data(), conf->data, conf->size);
      } else {
        g_confBytes.clear();
        g_confInfo = DepthFrameInfo{};
      }

      // Depth flags (FIXED: store correct metadata)
      if ((g_flagsMask & MLDepthCameraFlags_DepthFlags) &&
          frame->flags && frame->flags->data && frame->flags->size > 0) {
        MLDepthCameraFrameBuffer* fl = frame->flags;

        g_flagsInfo.width         = (int32_t)fl->width;
        g_flagsInfo.height        = (int32_t)fl->height;
        g_flagsInfo.strideBytes   = (int32_t)fl->stride;
        g_flagsInfo.captureTimeNs = ts;
        g_flagsInfo.bytesPerPixel = (int32_t)fl->bytes_per_unit;
        g_flagsInfo.format        = 0;

        g_flagsBytes.resize(fl->size);
        std::memcpy(g_flagsBytes.data(), fl->data, fl->size);
      } else {
        g_flagsBytes.clear();
        g_flagsInfo = DepthFrameInfo{};
      }

      // Raw depth (true raw)
      if ((g_flagsMask & MLDepthCameraFlags_RawDepthImage) &&
          frame->raw_depth_image && frame->raw_depth_image->data) {
        MLDepthCameraFrameBuffer* raw = frame->raw_depth_image;
        g_rawInfo.width         = (int32_t)raw->width;
        g_rawInfo.height        = (int32_t)raw->height;
        g_rawInfo.strideBytes   = (int32_t)raw->stride;
        g_rawInfo.captureTimeNs = ts;
        g_rawInfo.bytesPerPixel = (int32_t)raw->bytes_per_unit;
        g_rawInfo.format        = 0;

        g_rawBytes.resize(raw->size);
        std::memcpy(g_rawBytes.data(), raw->data, raw->size);
      } else {
        g_rawBytes.clear();
      }

      // Ambient raw depth
      if ((g_flagsMask & MLDepthCameraFlags_AmbientRawDepthImage) &&
          frame->ambient_raw_depth_image && frame->ambient_raw_depth_image->data) {
        MLDepthCameraFrameBuffer* amb = frame->ambient_raw_depth_image;
        g_ambientRawInfo.width         = (int32_t)amb->width;
        g_ambientRawInfo.height        = (int32_t)amb->height;
        g_ambientRawInfo.strideBytes   = (int32_t)amb->stride;
        g_ambientRawInfo.captureTimeNs = ts;
        g_ambientRawInfo.bytesPerPixel = (int32_t)amb->bytes_per_unit;
        g_ambientRawInfo.format        = 0;

        g_ambientRawBytes.resize(amb->size);
        std::memcpy(g_ambientRawBytes.data(), amb->data, amb->size);
      } else {
        g_ambientRawBytes.clear();
      }
    }

    MLDepthCameraReleaseDepthData(g_handle, &data);
  }
}

// ---------- Init ----------
bool MLDepthUnity_Init(uint32_t streamMask, uint32_t flagsMask, uint32_t frameRateEnum) {
  if (g_running.load()) return true;

  g_streamMask = streamMask;
  g_flagsMask  = flagsMask;
  g_frameRate  = frameRateEnum;

  // Optional strict fail if both requested
  if (g_rejectBothStreams && streamMask == (STREAM_LONG | STREAM_SHORT)) {
    LOGE("MLDepthUnity_Init: both streams requested (LONG|SHORT). Rejecting to avoid crash.");
    return false;
  }

  // Clamp FPS to something safe for the requested stream
  const uint32_t safeFpsEnum = PickSafeFps(streamMask, frameRateEnum);
  if (g_debug && safeFpsEnum != frameRateEnum) {
    LOGI("MLDepthUnity_Init: clamping fpsEnum %u -> %u for streams=%u (%s)",
         (unsigned)frameRateEnum, (unsigned)safeFpsEnum, (unsigned)streamMask, StreamMaskToStr(streamMask));
  }
  g_frameRate = safeFpsEnum;

  MLDepthCameraSettings settings;
  MLDepthCameraSettingsInit(&settings);

  // Use exactly what C# passes (streams bitmask)
  settings.streams = streamMask;

  // Configure only the enabled stream configs
  if (HasLong(streamMask)) {
    settings.stream_configs[MLDepthCameraFrameType_LongRange].flags = flagsMask;
    settings.stream_configs[MLDepthCameraFrameType_LongRange].frame_rate =
        (MLDepthCameraFrameRate)safeFpsEnum;
  }
  if (HasShort(streamMask)) {
    settings.stream_configs[MLDepthCameraFrameType_ShortRange].flags = flagsMask;
    settings.stream_configs[MLDepthCameraFrameType_ShortRange].frame_rate =
        (MLDepthCameraFrameRate)safeFpsEnum;
  }

  if (g_debug) {
    LOGI("MLDepthUnity_Init: streams=%u (%s) flags=%u requestedFps=%u safeFps=%u",
         (unsigned)streamMask, StreamMaskToStr(streamMask),
         (unsigned)flagsMask, (unsigned)frameRateEnum, (unsigned)safeFpsEnum);
    LogSettings(settings, streamMask);
  }

  MLResult r = MLDepthCameraConnect(&settings, &g_handle);
  if (r != MLResult_Ok || g_handle == ML_INVALID_HANDLE) {
    LOGE("MLDepthCameraConnect FAILED r=%d handle=%llu streams=%u flags=%u fpsEnum=%u",
         (int)r, (unsigned long long)g_handle,
         (unsigned)streamMask, (unsigned)flagsMask, (unsigned)safeFpsEnum);
    g_handle = ML_INVALID_HANDLE;
    return false;
  }

  LOGI("MLDepthCameraConnect OK handle=%llu", (unsigned long long)g_handle);

  g_running.store(true);
  g_thread = std::thread(CaptureLoop);
  return true;
}

// ---------- Copy out ----------
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

// FIXED: return correct info for confidence/flags
bool MLDepthUnity_TryGetLatestConfidence(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
  static bool once = false;
  if (!once) { once = true; LOGE("CONF GETTER: using g_confInfo (FIX CHECK)");}
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
  if (!g_running.load()) return;

  g_running.store(false);
  if (g_thread.joinable()) g_thread.join();

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

  // optional: clear infos too
  g_depthInfo = DepthFrameInfo{};
  g_confInfo  = DepthFrameInfo{};
  g_flagsInfo = DepthFrameInfo{};
  g_rawInfo   = DepthFrameInfo{};
  g_ambientRawInfo = DepthFrameInfo{};
}
