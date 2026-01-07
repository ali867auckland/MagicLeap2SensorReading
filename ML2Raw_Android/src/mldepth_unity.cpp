#include "mldepth_unity.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

#include <ml_depth_camera.h>

static std::atomic<bool> g_running{false};
static std::thread g_thread;

static MLHandle g_handle = ML_INVALID_HANDLE;

static std::mutex g_lock;

// Processed depth
static DepthFrameInfo g_depthInfo{};
static std::vector<uint8_t> g_depthBytes;

// Optional extras
static std::vector<uint8_t> g_confBytes;
static std::vector<uint8_t> g_flagsBytes;

// ✅ Raw depth
static DepthFrameInfo g_rawInfo{};
static std::vector<uint8_t> g_rawBytes;

// ✅ Ambient raw depth
static DepthFrameInfo g_ambientRawInfo{};
static std::vector<uint8_t> g_ambientRawBytes;

static uint32_t g_streamMask = 0;
static uint32_t g_flagsMask = 0;
static uint32_t g_frameRate = 0;

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

      // Confidence
      if ((g_flagsMask & MLDepthCameraFlags_Confidence) && frame->confidence && frame->confidence->data) {
        g_confBytes.resize(frame->confidence->size);
        std::memcpy(g_confBytes.data(), frame->confidence->data, frame->confidence->size);
      } else {
        g_confBytes.clear();
      }

      // Depth flags
      if ((g_flagsMask & MLDepthCameraFlags_DepthFlags) && frame->flags && frame->flags->data) {
        g_flagsBytes.resize(frame->flags->size);
        std::memcpy(g_flagsBytes.data(), frame->flags->data, frame->flags->size);
      } else {
        g_flagsBytes.clear();
      }

      // ✅ Raw depth (true raw)
      if ((g_flagsMask & MLDepthCameraFlags_RawDepthImage) && frame->raw_depth_image && frame->raw_depth_image->data) {
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

      // ✅ Ambient raw depth
      if ((g_flagsMask & MLDepthCameraFlags_AmbientRawDepthImage) && frame->ambient_raw_depth_image && frame->ambient_raw_depth_image->data) {
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

bool MLDepthUnity_Init(uint32_t streamMask, uint32_t flagsMask, uint32_t frameRateEnum) {
  if (g_running.load()) return true;

  g_streamMask = streamMask;
  g_flagsMask  = flagsMask;
  g_frameRate  = frameRateEnum;

  MLDepthCameraSettings settings;
  MLDepthCameraSettingsInit(&settings);

  settings.streams = streamMask;

  settings.stream_configs[MLDepthCameraFrameType_LongRange].flags      = flagsMask;
  settings.stream_configs[MLDepthCameraFrameType_LongRange].frame_rate = (MLDepthCameraFrameRate)frameRateEnum;

  settings.stream_configs[MLDepthCameraFrameType_ShortRange].flags      = flagsMask;
  settings.stream_configs[MLDepthCameraFrameType_ShortRange].frame_rate = (MLDepthCameraFrameRate)frameRateEnum;

  MLResult r = MLDepthCameraConnect(&settings, &g_handle);
  if (r != MLResult_Ok || g_handle == ML_INVALID_HANDLE) {
    g_handle = ML_INVALID_HANDLE;
    return false;
  }

  g_running.store(true);
  g_thread = std::thread(CaptureLoop);
  return true;
}

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
  return CopyOut(g_confBytes, g_depthInfo, outInfo, outBytes, cap, written);
}

bool MLDepthUnity_TryGetLatestDepthFlags(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
  return CopyOut(g_flagsBytes, g_depthInfo, outInfo, outBytes, cap, written);
}

// ✅ Raw getters
bool MLDepthUnity_TryGetLatestRawDepth(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
  return CopyOut(g_rawBytes, g_rawInfo, outInfo, outBytes, cap, written);
}

bool MLDepthUnity_TryGetLatestAmbientRawDepth(DepthFrameInfo* outInfo, uint8_t* outBytes, int32_t cap, int32_t* written) {
  return CopyOut(g_ambientRawBytes, g_ambientRawInfo, outInfo, outBytes, cap, written);
}

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
}
