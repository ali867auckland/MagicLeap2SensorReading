#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// RGB frame with synchronized camera pose
typedef struct RGBFrameWithPose {
    // Frame info
    int32_t width;
    int32_t height;
    int32_t strideBytes;
    int32_t format;           // MLCameraOutputFormat
    int64_t timestampNs;
    
    // Camera pose at frame capture (from CV Camera API)
    float pose_rotation_x;
    float pose_rotation_y;
    float pose_rotation_z;
    float pose_rotation_w;
    float pose_position_x;
    float pose_position_y;
    float pose_position_z;
    int32_t pose_valid;       // 1 if pose was successfully retrieved, 0 otherwise
    int32_t pose_result_code; // MLResult from CVCameraGetFramePose
    
    // Intrinsics (if available)
    float fx, fy;             // Focal length
    float cx, cy;             // Principal point
} RGBFrameWithPose;

// Camera capture mode
typedef enum {
    RGBCaptureMode_Preview = 0,    // Lower res, higher FPS
    RGBCaptureMode_Video = 1,      // Video capture mode
    RGBCaptureMode_Image = 2       // Still image capture
} RGBCaptureMode;

// Initialize RGB camera
// mode: capture mode (preview/video/image)
// NOTE: CV Camera (CVCameraNativeConsumer) must be initialized separately for poses to work.
//       RGB camera will call MLCVCameraUnity_GetPose() internally for synchronized poses.
bool MLRGBCameraUnity_Init(RGBCaptureMode mode);

// Start capturing
bool MLRGBCameraUnity_StartCapture();

// Stop capturing
void MLRGBCameraUnity_StopCapture();

// Try to get the latest frame with its pose
// timeout_ms: how long to wait for a frame
// out_info: frame metadata and pose
// out_bytes: pixel data buffer (caller-provided)
// capacity_bytes: size of out_bytes buffer
// out_bytes_written: actual bytes written (or required size if too small)
// Returns true if frame was retrieved successfully
bool MLRGBCameraUnity_TryGetLatestFrame(
    uint32_t timeout_ms,
    RGBFrameWithPose* out_info,
    uint8_t* out_bytes,
    int32_t capacity_bytes,
    int32_t* out_bytes_written
);

// Get number of frames captured so far
uint64_t MLRGBCameraUnity_GetFrameCount();

// Check if initialized and capturing
bool MLRGBCameraUnity_IsCapturing();

// Shutdown and release resources
void MLRGBCameraUnity_Shutdown();

#ifdef __cplusplus
}
#endif