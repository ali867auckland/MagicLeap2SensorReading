#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

// identifiers_mask: MLWorldCameraIdentifier bitmask: Left=1, Right=2, Center=4
bool MLWorldCamUnity_Init(uint32_t identifiers_mask);

// timeout_ms: 0 for non-blocking poll, or >0 to wait
// out_bytes: caller-provided buffer
// capacity_bytes: size of out_bytes
// out_bytes_written:
//   - on success: number of bytes copied into out_bytes
//   - on failure due to capacity: required bytes (so C# can resize and retry)
bool MLWorldCamUnity_TryGetLatest(
  uint32_t timeout_ms,
  WorldCamFrameInfo* out_info,
  uint8_t* out_bytes,
  int32_t capacity_bytes,
  int32_t* out_bytes_written
);

void MLWorldCamUnity_Shutdown();

#ifdef __cplusplus
} // extern "C"
#endif
