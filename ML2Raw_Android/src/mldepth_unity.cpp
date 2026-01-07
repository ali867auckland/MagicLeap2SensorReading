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

// We store the latest buffers here (copied from ML system-owned memory)
static std::mutex g_lock;
static DepthFrameInfo g_depthInfo{};
static std::vector<uint8_t> g_depthBytes;
static std::vector<uint8_t> g_confBytes;
static std::vector<uint8_t> g_flagsBytes;

static uint32_t g_streamMask = 0;
static uint32_t g_flagsMask = 0;
static uint32_t g_frameRate = 0;

// Helper: map MLResult
static bool Ok(MLResult r) { return r == MLResult_Ok; }

static void CaptureLoop() {
  while (g_running.load()) {
    MLDepthCameraData data;
    MLDepthCameraDataInit(&data);

    MLResult r = MLDepthCameraGetLatestDepthData(g_handle, /*timeout_ms*/ 200, &data);

    if (r == MLResult_Timeout) {
      continue;
    }
    if (r != MLResult_Ok) {
      continue;
    }

    if (data.frame_count == 0 || data.frames == nullptr) {
      MLDepthCameraReleaseDepthData(g_handle, &data);
      continue;
    }

    // Start simple: just take the first frame returned.
    MLDepthCameraFrame* frame = &data.frames[0];

    // depth_image is a pointer and may be null if not requested
    MLDepthCameraFrameBuffer* depth = frame->depth_image;
    if (!depth || !depth->data || depth->size == 0) {
      MLDepthCameraReleaseDepthData(g_handle, &data);
      continue;
    }

    {
      std::lock_guard<std::mutex> guard(g_lock);

      g_depthInfo.width         = (int32_t)depth->width;
      g_depthInfo.height        = (int32_t)depth->height;
      g_depthInfo.strideBytes   = (int32_t)depth->stride;
      g_depthInfo.captureTimeNs = (int64_t)frame->frame_timestamp; // MLTime
      g_depthInfo.bytesPerPixel = (int32_t)depth->bytes_per_unit;
      g_depthInfo.format        = 0;

      g_depthBytes.resize(depth->size);
      std::memcpy(g_depthBytes.data(), depth->data, depth->size);

      // Optional confidence
      if ((g_flagsMask & MLDepthCameraFlags_Confidence) && frame->confidence && frame->confidence->data) {
        g_confBytes.resize(frame->confidence->size);
        std::memcpy(g_confBytes.data(), frame->confidence->data, frame->confidence->size);
      } else {
        g_confBytes.clear();
      }

      // Optional depth flags
      if ((g_flagsMask & MLDepthCameraFlags_DepthFlags) && frame->flags && frame->flags->data) {
        g_flagsBytes.resize(frame->flags->size);
        std::memcpy(g_flagsBytes.data(), frame->flags->data, frame->flags->size);
      } else {
        g_flagsBytes.clear();
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

  // The API note says single stream is supported; pick ONE stream to start.
  settings.streams = streamMask;

  // Apply requested flags + FPS to both configs (only enabled stream is read).
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


static bool CopyOut(const std::vector<uint8_t>& src, DepthFrameInfo* outInfo,
                    uint8_t* outBytes, int32_t cap, int32_t* written) {
  if (!outInfo || !outBytes || !written) return false;
  std::lock_guard<std::mutex> guard(g_lock);
  if (src.empty()) return false;
  int32_t n = (int32_t)src.size();
  if (n > cap) return false;

  *outInfo = g_depthInfo;
  std::memcpy(outBytes, src.data(), n);
  *written = n;
  return true;
}

bool MLDepthUnity_TryGetLatestDepth(uint32_t /*timeoutMs*/,
                                   DepthFrameInfo* outInfo,
                                   uint8_t* outDepthBytes,
                                   int32_t capacityBytes,
                                   int32_t* bytesWritten) {
  return CopyOut(g_depthBytes, outInfo, outDepthBytes, capacityBytes, bytesWritten);
}

bool MLDepthUnity_TryGetLatestConfidence(DepthFrameInfo* outInfo,
                                        uint8_t* outBytes,
                                        int32_t capacityBytes,
                                        int32_t* bytesWritten) {
  return CopyOut(g_confBytes, outInfo, outBytes, capacityBytes, bytesWritten);
}

bool MLDepthUnity_TryGetLatestDepthFlags(DepthFrameInfo* outInfo,
                                        uint8_t* outBytes,
                                        int32_t capacityBytes,
                                        int32_t* bytesWritten) {
  return CopyOut(g_flagsBytes, outInfo, outBytes, capacityBytes, bytesWritten);
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
}
