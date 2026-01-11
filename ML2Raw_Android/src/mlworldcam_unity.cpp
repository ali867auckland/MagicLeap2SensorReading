#include "mlworldcam_unity.h"

#include <algorithm>
#include <cstring>

#include <ml_world_camera.h>

// Your project already has this helper (based on your build error).
#include "mlperception_service.h"
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

  // identifiers_mask is MLWorldCameraIdentifier bitmask
  settings.cameras = (MLWorldCameraIdentifier)identifiers_mask;

  // Request both modes if supported (you’ll get frameType telling you which you got).
  settings.mode = (MLWorldCameraMode)(
      MLWorldCameraMode_NormalExposure | MLWorldCameraMode_LowExposure);

  MLResult r = MLWorldCameraConnect(&settings, &g_wc_handle);
  if (r != MLResult_Ok || g_wc_handle == ML_INVALID_HANDLE) {
    g_wc_handle = ML_INVALID_HANDLE;
    return false;
  }

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

  MLWorldCameraData* data = nullptr;
  MLResult r = MLWorldCameraGetLatestWorldCameraData(g_wc_handle, timeout_ms, &data);

  // IMPORTANT: timeout is normal (no new frame yet).
  if (r == MLResult_Timeout) {
    return false;
  }

  // Any other failure.
  if (r != MLResult_Ok || data == nullptr) {
    // Never release nullptr.
    return false;
  }

  bool ok = false;

  // Data can contain multiple frames (left/center/right) depending on cameras mask.
  if (data->frame_count > 0 && data->frames != nullptr) {
    const MLWorldCameraFrame& f = data->frames[0]; // simplest: first frame

    // Fill info
    out_info->camId        = (int32_t)f.id;
    out_info->frameType    = (int32_t)f.frame_type;
    out_info->timestampNs  = (int64_t)f.timestamp;

    // Frame buffer
    const MLWorldCameraFrameBuffer& fb = f.frame_buffer;

    out_info->width        = (int32_t)fb.width;
    out_info->height       = (int32_t)fb.height;
    out_info->strideBytes  = (int32_t)fb.stride;
    out_info->bytesPerPixel= (int32_t)fb.bytes_per_pixel;

    const int32_t required = (int32_t)fb.size;
    if (required <= 0 || fb.data == nullptr) {
      ok = false;
    } else if (required > capacity_bytes) {
      // Tell caller how big they need to be.
      *out_bytes_written = required;
      ok = false;
    } else {
      std::memcpy(out_bytes, fb.data, (size_t)required);
      *out_bytes_written = required;
      ok = true;
    }
  }

  // ALWAYS release data if GetLatest returned Ok and data != nullptr
  MLWorldCameraReleaseCameraData(g_wc_handle, data);

  return ok;
}

extern "C" void MLWorldCamUnity_Shutdown() {
  std::lock_guard<std::mutex> lock(g_wc_mutex);
  if (!g_wc_inited) return;

  SafeDisconnect();
  MLPerceptionService_Shutdown();

  g_wc_inited = false;
}
