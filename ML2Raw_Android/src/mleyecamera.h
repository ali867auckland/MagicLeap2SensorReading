#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Eye camera identifiers (matches MLEyeCameraIdentifier)
typedef enum {
    EyeCameraID_None = 0,
    EyeCameraID_LeftTemple = 1 << 0,   // 1
    EyeCameraID_LeftNasal = 1 << 1,    // 2
    EyeCameraID_RightNasal = 1 << 2,   // 4
    EyeCameraID_RightTemple = 1 << 3,  // 8
    EyeCameraID_All = 15  // All cameras
} EyeCameraID;

// Eye camera frame info
typedef struct EyeCameraFrameInfo {
    uint32_t camera_id;        // Which camera (1=LeftTemple, 2=LeftNasal, 4=RightNasal, 8=RightTemple)
    int64_t frame_number;      // Frame index
    int64_t timestamp_ns;      // MLTime timestamp
    uint32_t width;            // Image width
    uint32_t height;           // Image height
    uint32_t stride;           // Row stride in bytes
    uint32_t bytes_per_pixel;  // Bytes per pixel
    uint32_t size;             // Total data size
} EyeCameraFrameInfo;

// Initialize eye camera
// camera_mask: Bitmask of cameras to enable (e.g., EyeCameraID_All)
bool MLEyeCameraUnity_Init(uint32_t camera_mask);

// Try to get latest frame from specific camera
// camera_id: Which camera to query (1, 2, 4, or 8)
// out_info: Frame metadata
// out_bytes: Buffer to receive image data
// capacity_bytes: Size of out_bytes buffer
// bytes_written: Actual bytes written
bool MLEyeCameraUnity_TryGetLatestFrame(
    uint32_t camera_id,
    EyeCameraFrameInfo* out_info,
    uint8_t* out_bytes,
    int32_t capacity_bytes,
    int32_t* bytes_written);

// Check if camera has new frame available
bool MLEyeCameraUnity_HasNewFrame(uint32_t camera_id);

// Get total frame count for a camera
uint64_t MLEyeCameraUnity_GetFrameCount(uint32_t camera_id);

bool MLEyeCameraUnity_IsInitialized();
void MLEyeCameraUnity_Shutdown();

#ifdef __cplusplus
}
#endif