#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DepthFrameInfo {
  int32_t width;
  int32_t height;
  int32_t strideBytes;
  int64_t captureTimeNs;
  int32_t bytesPerPixel;
  int32_t format;
} DepthFrameInfo;

bool MLDepthUnity_Init(uint32_t streamMask, uint32_t flagsMask, uint32_t frameRateEnum);

bool MLDepthUnity_TryGetLatestDepth(
    uint32_t timeoutMs,
    DepthFrameInfo* outInfo,
    uint8_t* outDepthBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten);

bool MLDepthUnity_TryGetLatestConfidence(
    DepthFrameInfo* outInfo,
    uint8_t* outBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten);

bool MLDepthUnity_TryGetLatestDepthFlags(
    DepthFrameInfo* outInfo,
    uint8_t* outBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten);

// ✅ Raw depth (true raw) and optional ambient raw depth.
bool MLDepthUnity_TryGetLatestRawDepth(
    DepthFrameInfo* outInfo,
    uint8_t* outBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten);

bool MLDepthUnity_TryGetLatestAmbientRawDepth(
    DepthFrameInfo* outInfo,
    uint8_t* outBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten);

void MLDepthUnity_Shutdown();

#ifdef __cplusplus
}
#endif
