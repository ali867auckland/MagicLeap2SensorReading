#include "mlworldcam_unity.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

#include <ml_perception.h>
#include <ml_world_camera.h>
#include <android/log.h>

#define LOG_TAG "MLWorldCamUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::atomic<bool> g_running{false};
static std::thread g_thread;

static MLHandle g_wc_handle = ML_INVALID_HANDLE;
static uint32_t g_ident_mask = (uint32_t)MLWorldCameraIdentifier_All;

// A very small ring buffer just to keep the latest frame bytes.
// (Your C# reads from this via TryGetLatest)
struct FrameSlot {
  WCFrameInfo info{};
  std::vector<uint8_t> bytes;
};

static std::mutex g_mtx;
static FrameSlot g_latest;
static std::atomic<bool> g_has_latest{false};

// Forward
static void CaptureLoop();

extern "C" {

bool MLWorldCamUnity_Init(uint32_t identifiers_mask) {
  if (g_running.load()) {
    LOGI("Init called but already running.");
    return true;
  }

  g_ident_mask = identifiers_mask;

  MLResult pr = MLPerceptionService_Startup();
  if (pr != MLResult_Ok) {
    LOGE("MLPerceptionService_Startup failed: %d", (int)pr);
    return false;
  }

  // IMPORTANT: must init settings before Connect (sets version + defaults)
  MLWorldCameraSettings settings;
  MLWorldCameraSettingsInit(&settings); // <-- FIX

  settings.mode = MLWorldCameraMode_NormalExposure;
  settings.cameras = (MLWorldCameraIdentifier)g_ident_mask;

  MLResult cr = MLWorldCameraConnect(&settings, &g_wc_handle);
  if (cr != MLResult_Ok || g_wc_handle == ML_INVALID_HANDLE) {
    LOGE("MLWorldCameraConnect failed: %d (handle=%lld)", (int)cr, (long long)g_wc_handle);
    MLPerceptionService_Shutdown();
    g_wc_handle = ML_INVALID_HANDLE;
    return false;
  }

  {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_latest.bytes.clear();
    g_has_latest.store(false);
  }

  g_running.store(true);
  g_thread = std::thread(CaptureLoop);

  LOGI("WorldCam init OK. mask=0x%X handle=%lld", g_ident_mask, (long long)g_wc_handle);
  return true;
}

void MLWorldCamUnity_Shutdown() {
  if (!g_running.load()) {
    return;
  }

  g_running.store(false);
  if (g_thread.joinable()) g_thread.join();

  if (g_wc_handle != ML_INVALID_HANDLE) {
    MLResult dr = MLWorldCameraDisconnect(g_wc_handle);
    LOGI("MLWorldCameraDisconnect: %d", (int)dr);
    g_wc_handle = ML_INVALID_HANDLE;
  }

  MLResult ps = MLPerceptionService_Shutdown();
  LOGI("MLPerceptionService_Shutdown: %d", (int)ps);

  LOGI("WorldCam shutdown done.");
}

// Copy latest stored bytes into caller buffer (C#)
// Returns true if a frame was copied.
bool MLWorldCamUnity_TryGetLatest(
  uint32_t timeout_ms,
  WCFrameInfo* out_info,
  uint8_t* out_bytes,
  int32_t capacity_bytes,
  int32_t* out_bytes_written
) {
  (void)timeout_ms; // we don't block; capture thread owns blocking

  if (out_bytes_written) *out_bytes_written = 0;
  if (!out_info || !out_bytes || capacity_bytes <= 0 || !out_bytes_written) return false;

  if (!g_has_latest.load()) return false;

  std::lock_guard<std::mutex> lk(g_mtx);

  if (!g_has_latest.load()) return false;
  const int32_t sz = (int32_t)g_latest.bytes.size();
  if (sz <= 0) return false;

  const int32_t n = (sz <= capacity_bytes) ? sz : capacity_bytes;
  std::memcpy(out_bytes, g_latest.bytes.data(), (size_t)n);
  *out_bytes_written = n;
  *out_info = g_latest.info;

  return true;
}

} // extern "C"

static void CaptureLoop() {
  LOGI("CaptureLoop start");

  while (g_running.load()) {
    if (g_wc_handle == ML_INVALID_HANDLE) {
      // Should never happen, but avoid calling into ML with a bad handle
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    MLWorldCameraData* data = nullptr;

    // IMPORTANT: out_data must be a valid pointer (we pass &data).
    MLResult r = MLWorldCameraGetLatestWorldCameraData(g_wc_handle, /*timeout_ms*/ 100, &data);

    if (r == MLResult_Timeout) {
      continue; // no new frame yet
    }

    if (r != MLResult_Ok) {
      LOGE("GetLatestWorldCameraData failed: %d (data=%p)", (int)r, (void*)data);
      continue;
    }

    // ML docs: out_data may be NULL if no valid data available at this time.
    if (!data || data->frame_count == 0) {
      // still release if data is non-null
      if (data) {
        MLResult rr = MLWorldCameraReleaseCameraData(g_wc_handle, data);
        if (rr != MLResult_Ok) LOGE("ReleaseCameraData failed: %d", (int)rr);
      }
      continue;
    }

    // Grab the first available frame (you can expand to choose left/right/center)
    MLWorldCameraFrame* f = &data->frames[0];

    if (f->frame_buffer.data && f->frame_buffer.size > 0) {
      WCFrameInfo info{};
      info.cam_id = (int32_t)f->id;
      info.frame_number = (int64_t)f->frame_number;
      info.timestamp_ns = (uint64_t)f->frame_timestamp;
      info.width = (int32_t)f->frame_buffer.width;
      info.height = (int32_t)f->frame_buffer.height;
      info.stride = (int32_t)f->frame_buffer.stride;
      info.format = (int32_t)f->frame_buffer.format;
      info.bytes = (int32_t)f->frame_buffer.size;

      {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_latest.info = info;
        g_latest.bytes.resize((size_t)f->frame_buffer.size);
        std::memcpy(g_latest.bytes.data(), f->frame_buffer.data, (size_t)f->frame_buffer.size);
        g_has_latest.store(true);
      }
    }

    MLResult rr = MLWorldCameraReleaseCameraData(g_wc_handle, data);
    if (rr != MLResult_Ok) {
      LOGE("MLWorldCameraReleaseCameraData failed: %d", (int)rr);
    }
  }

  LOGI("CaptureLoop end");
}
