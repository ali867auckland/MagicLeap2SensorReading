#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WorldCamFrameInfo {
  int32_t camId;          // 0=Left, 1=Center, 2=Right (we normalize)
  int64_t frameNumber;
  int64_t timestampNs;    // MLTime (ns)
  int32_t width;
  int32_t height;
  int32_t strideBytes;
  int32_t bytesPerPixel;
  int32_t frameType;      // MLWorldCameraFrameType as int
} WorldCamFrameInfo;

// identifiersMask uses MLWorldCameraIdentifier bits (Left/Right/Center/All)
bool MLWorldCamUnity_Init(uint32_t identifiersMask);

// Non-blocking-ish: uses MLWorldCameraGetLatestWorldCameraData(handle, timeout_ms,...)
// If capacityBytes is too small, returns false and sets bytesWritten = required size.
bool MLWorldCamUnity_TryGetLatest(
    uint32_t timeoutMs,
    WorldCamFrameInfo* outInfo,
    uint8_t* outBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten);

void MLWorldCamUnity_Shutdown();

#ifdef __cplusplus
}
#endif
