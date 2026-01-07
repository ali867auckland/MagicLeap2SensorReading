#include "mlworldcam_unity.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

#include <ml_world_camera.h>

static std::atomic<bool> g_wc_running{false};
static std::thread g_wc_thread;
static MLHandle g_wc_handle = ML_INVALID_HANDLE;

static std::mutex g_wc_mtx;
static std::vector<uint8_t> g_wc_latest;
static WorldCamFrameInfo g_wc_info{};
static std::atomic<bool> g_wc_hasFrame{false};

static int NormalizeCamId(MLWorldCameraIdentifier id) {
  // We normalize to: 0=Left, 1=Center, 2=Right
  if (id == MLWorldCameraIdentifier_Left)   return 0;
  if (id == MLWorldCameraIdentifier_Center) return 1;
  if (id == MLWorldCameraIdentifier_Right)  return 2;
  return -1;
}

static void CaptureLoop() {
  while (g_wc_running.load()) {
    MLWorldCameraData* data = nullptr;
    MLResult r = MLWorldCameraGetLatestWorldCameraData(g_wc_handle, 200, &data);

    if (r == MLResult_Timeout) continue;
    if (r != MLResult_Ok || data == nullptr) continue;

    // data->frame_count frames in data->frames :contentReference[oaicite:6]{index=6}
    // Each frame has one frame_buffer (not multi-plane) :contentReference[oaicite:7]{index=7}

    MLWorldCameraFrame* frames = data->frames;
    uint8_t n = data->frame_count;

    // Prefer Center if available, else first
    int bestIndex = -1;
    for (uint8_t i = 0; i < n; i++) {
      if (frames[i].id == MLWorldCameraIdentifier_Center) { bestIndex = (int)i; break; }
    }
    if (bestIndex < 0 && n > 0) bestIndex = 0;

    if (bestIndex >= 0) {
      const MLWorldCameraFrame& f = frames[bestIndex];
      const MLWorldCameraFrameBuffer& b = f.frame_buffer;

      if (b.data != nullptr && b.size > 0) {
        std::lock_guard<std::mutex> lk(g_wc_mtx);
        g_wc_latest.resize((size_t)b.size);
        std::memcpy(g_wc_latest.data(), b.data, (size_t)b.size);

        g_wc_info.camId = NormalizeCamId(f.id);
        g_wc_info.frameNumber = (int64_t)f.frame_number;
        g_wc_info.timestampNs = (int64_t)f.timestamp; // MLTime (ns)
        g_wc_info.width = (int32_t)b.width;
        g_wc_info.height = (int32_t)b.height;
        g_wc_info.strideBytes = (int32_t)b.stride;
        g_wc_info.bytesPerPixel = (int32_t)b.bytes_per_pixel;
        g_wc_info.frameType = (int32_t)f.frame_type;

        g_wc_hasFrame.store(true);
      }
    }

    MLWorldCameraReleaseCameraData(g_wc_handle, data);
  }
}

bool MLWorldCamUnity_Init(uint32_t identifiersMask) {
  if (g_wc_running.load()) return true;

  MLWorldCameraSettings settings;
  MLWorldCameraSettingsInit(&settings);

  // In this MLSDK header the settings struct is opaque here; the API exists, but
  // some versions allow selecting cameras, others don’t. We'll just connect.
  // If your MLWorldCameraSettings has a "cameras"/"camera_id" field, you can set it.
  (void)identifiersMask;

  MLResult r = MLWorldCameraConnect(&settings, &g_wc_handle);
  if (r != MLResult_Ok) return false;

  g_wc_running.store(true);
  g_wc_thread = std::thread(CaptureLoop);
  return true;
}

bool MLWorldCamUnity_TryGetLatest(
    uint32_t timeoutMs,
    WorldCamFrameInfo* outInfo,
    uint8_t* outBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten) {

  // If we don't have a cached frame yet, we can do a one-shot poll with timeoutMs
  if (!g_wc_hasFrame.load()) {
    MLWorldCameraData* data = nullptr;
    MLResult r = MLWorldCameraGetLatestWorldCameraData(g_wc_handle, timeoutMs, &data);
    if (r != MLResult_Ok || data == nullptr) return false;

    MLWorldCameraFrame* frames = data->frames;
    uint8_t n = data->frame_count;

    int bestIndex = -1;
    for (uint8_t i = 0; i < n; i++) {
      if (frames[i].id == MLWorldCameraIdentifier_Center) { bestIndex = (int)i; break; }
    }
    if (bestIndex < 0 && n > 0) bestIndex = 0;

    bool ok = false;
    if (bestIndex >= 0) {
      const MLWorldCameraFrame& f = frames[bestIndex];
      const MLWorldCameraFrameBuffer& b = f.frame_buffer;

      if (bytesWritten) *bytesWritten = (int32_t)b.size;
      if (b.size > (uint32_t)capacityBytes) {
        MLWorldCameraReleaseCameraData(g_wc_handle, data);
        return false;
      }

      if (b.data && b.size > 0) {
        std::memcpy(outBytes, b.data, (size_t)b.size);
        if (outInfo) {
          outInfo->camId = NormalizeCamId(f.id);
          outInfo->frameNumber = (int64_t)f.frame_number;
          outInfo->timestampNs = (int64_t)f.timestamp;
          outInfo->width = (int32_t)b.width;
          outInfo->height = (int32_t)b.height;
          outInfo->strideBytes = (int32_t)b.stride;
          outInfo->bytesPerPixel = (int32_t)b.bytes_per_pixel;
          outInfo->frameType = (int32_t)f.frame_type;
        }
        ok = true;
      }
    }

    MLWorldCameraReleaseCameraData(g_wc_handle, data);
    return ok;
  }

  std::lock_guard<std::mutex> lk(g_wc_mtx);
  int32_t n = (int32_t)g_wc_latest.size();
  if (bytesWritten) *bytesWritten = n;
  if (n > capacityBytes) return false;

  if (n > 0) std::memcpy(outBytes, g_wc_latest.data(), (size_t)n);
  if (outInfo) *outInfo = g_wc_info;

  g_wc_hasFrame.store(false);
  return true;
}

void MLWorldCamUnity_Shutdown() {
  if (g_wc_running.load()) {
    g_wc_running.store(false);
    if (g_wc_thread.joinable()) g_wc_thread.join();
  }

  if (g_wc_handle != ML_INVALID_HANDLE) {
    MLWorldCameraDisconnect(g_wc_handle);
    g_wc_handle = ML_INVALID_HANDLE;
  }

  std::lock_guard<std::mutex> lk(g_wc_mtx);
  g_wc_latest.clear();
  g_wc_hasFrame.store(false);
}
