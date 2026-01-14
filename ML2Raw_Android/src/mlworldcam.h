#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Camera identifiers (bitmask)
#define WORLDCAM_LEFT   (1u << 0)  // 1
#define WORLDCAM_RIGHT  (1u << 1)  // 2
#define WORLDCAM_CENTER (1u << 2)  // 4
#define WORLDCAM_ALL    (WORLDCAM_LEFT | WORLDCAM_RIGHT | WORLDCAM_CENTER)  // 7

// Small POD struct for Unity to consume.
typedef struct WorldCamFrameInfo {
  int32_t  camId;          // MLWorldCameraIdentifier bit (1,2,4) cast to int
  int32_t  width;
  int32_t  height;
  int32_t  strideBytes;
  int32_t  bytesPerPixel;
  int32_t  frameType;      // MLWorldCameraFrameType as int
  int64_t  timestampNs;    // MLTime (ns)
} WorldCamFrameInfo;

// Initialize world cameras
// identifiers_mask: MLWorldCameraIdentifier bitmask: Left=1, Right=2, Center=4, All=7
bool MLWorldCamUnity_Init(uint32_t identifiers_mask);

// Get frame from specific camera
// camId: which camera to get (1=LEFT, 2=RIGHT, 4=CENTER)
// out_bytes: caller-provided buffer
// capacity_bytes: size of out_bytes
// out_bytes_written:
//   - on success: number of bytes copied into out_bytes
//   - on failure due to capacity: required bytes (so C# can resize and retry)
bool MLWorldCamUnity_TryGetLatest(
  uint32_t camId,
  WorldCamFrameInfo* out_info,
  uint8_t* out_bytes,
  int32_t capacity_bytes,
  int32_t* out_bytes_written
);

// Get count of cameras with new frames available
int32_t MLWorldCamUnity_GetAvailableCount(void);

// Check if specific camera has new frame
bool MLWorldCamUnity_HasNewFrame(uint32_t camId);

// Shutdown world cameras
void MLWorldCamUnity_Shutdown(void);

#ifdef __cplusplus
} // extern "C"
#endif