#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// What we return to C# (simple POD)
typedef struct DepthFrameInfo {
  int32_t width;
  int32_t height;
  int32_t strideBytes;
  int64_t captureTimeNs;   // depends on ML API; keep as int64
  int32_t bytesPerPixel;   // for depth float = 4
  int32_t format;          // optional
} DepthFrameInfo;

// Init / shutdown
// streamMask: use MLDepthCameraStream_LongRange or ShortRange (or both, if supported by your SDK)
// flagsMask:  MLDepthCameraFlags_DepthImage | Confidence | DepthFlags | RawDepth etc
// frameRateEnum: MLDepthCameraFrameRate_* value (0..)
bool MLDepthUnity_Init(uint32_t streamMask, uint32_t flagsMask, uint32_t frameRateEnum);

// Non-blocking pull (internally uses a native thread)
// timeoutMs: how long native thread blocks in MLDepthCameraGetLatestDepthData (suggest 100..500)
// outInfo: filled if frame available
// outDepthBytes: buffer for raw depth image bytes (float32 pixels typically)
// capacityBytes: size of outDepthBytes
// bytesWritten: how many bytes copied
bool MLDepthUnity_TryGetLatestDepth(
    uint32_t timeoutMs,
    DepthFrameInfo* outInfo,
    uint8_t* outDepthBytes,
    int32_t capacityBytes,
    int32_t* bytesWritten);

// optional extras (if you requested them in flags)
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

void MLDepthUnity_Shutdown();

#ifdef __cplusplus
}
#endif
