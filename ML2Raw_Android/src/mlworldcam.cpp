#include "mlworldcam_unity.h"

#include <algorithm>
#include <cstring>

#include <ml_world_camera.h>
#include "mlperception_service.h"

#include <android/log.h>
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MLWorldCamUnity", __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MLWorldCamUnity", __VA_ARGS__)

#include <mutex>
static std::mutex g_wc_mutex;

static MLHandle g_wc_handle = ML_INVALID_HANDLE;
static bool     g_wc_inited = false;

static void SafeDisconnect() {
  if (g_wc_handle != ML_INVALID_HANDLE) {
    MLWorldCameraDisconnect(g_wc_handle);
    g_wc_handle = ML_INVALID_HANDLE;
  }
}

extern "C" bool MLWorldCamUnity_Init(uint32_t identifiers_mask) {
  std::lock_guard<std::mutex> lock(g_wc_mutex);
  if (g_wc_inited) return true;

  MLWorldCameraSettings settings;
  MLWorldCameraSettingsInit(&settings);

  settings.cameras = (MLWorldCameraIdentifier)identifiers_mask;

  // Use ONLY normal exposure (mode=1) - dual mode can fail on some firmware
  settings.mode = MLWorldCameraMode_NormalExposure;

  ALOGI("MLWorldCamUnity_Init: cameras=%u mode=%d", identifiers_mask, (int)settings.mode);

  MLResult r = MLWorldCameraConnect(&settings, &g_wc_handle);
  
  if (r != MLResult_Ok || g_wc_handle == ML_INVALID_HANDLE) {
    ALOGE("MLWorldCameraConnect FAILED: r=%d", (int)r);
    g_wc_handle = ML_INVALID_HANDLE;
    return false;
  }

  ALOGI("MLWorldCameraConnect OK: handle=%llu", (unsigned long long)g_wc_handle);
  g_wc_inited = true;
  return true;
}

extern "C" bool MLWorldCamUnity_TryGetLatest(
    uint32_t timeout_ms,
    WorldCamFrameInfo* out_info,
    uint8_t* out_bytes,
    int32_t capacity_bytes,
    int32_t* out_bytes_written) {
      
  std::lock_guard<std::mutex> lock(g_wc_mutex);

  if (!out_info || !out_bytes || capacity_bytes <= 0 || !out_bytes_written) {
    if (out_bytes_written) *out_bytes_written = 0;
    return false;
  }

  *out_bytes_written = 0;

  if (!g_wc_inited || g_wc_handle == ML_INVALID_HANDLE) {
    return false;
  }

  // TRY THIS: Initialize the data structure
  MLWorldCameraData data;
  MLWorldCameraDataInit(&data);
  MLWorldCameraData* data_ptr = &data;
  
  MLResult r = MLWorldCameraGetLatestWorldCameraData(g_wc_handle, (uint64_t)timeout_ms, &data_ptr);

  // Log failures
  static int errCount = 0;
  if (r != MLResult_Ok && r != MLResult_Timeout && errCount < 10) {
    ALOGE("GetLatestWorldCameraData failed: r=%d data_ptr=%p", (int)r, data_ptr);
    errCount++;
  }

  if (r == MLResult_Timeout) {
    return false;
  }

  if (r != MLResult_Ok || data_ptr == nullptr) {
    return false;
  }

  bool ok = false;

  if (data_ptr->frame_count > 0 && data_ptr->frames != nullptr) {
    const MLWorldCameraFrame& f = data_ptr->frames[0];

    out_info->camId        = (int32_t)f.id;
    out_info->frameType    = (int32_t)f.frame_type;
    out_info->timestampNs  = (int64_t)f.timestamp;

    const MLWorldCameraFrameBuffer& fb = f.frame_buffer;

    out_info->width        = (int32_t)fb.width;
    out_info->height       = (int32_t)fb.height;
    out_info->strideBytes  = (int32_t)fb.stride;
    out_info->bytesPerPixel= (int32_t)fb.bytes_per_pixel;

    const int32_t required = (int32_t)fb.size;
    if (required <= 0 || fb.data == nullptr) {
      ok = false;
    } else if (required > capacity_bytes) {
      *out_bytes_written = required;
      ok = false;
    } else {
      std::memcpy(out_bytes, fb.data, (size_t)required);
      *out_bytes_written = required;
      ok = true;
    }
  }

  MLWorldCameraReleaseCameraData(g_wc_handle, data_ptr);
  return ok;
}

extern "C" void MLWorldCamUnity_Shutdown() {
  std::lock_guard<std::mutex> lock(g_wc_mutex);
  if (!g_wc_inited) return;

  SafeDisconnect();
  g_wc_inited = false;
}