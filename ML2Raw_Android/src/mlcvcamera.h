#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Camera ID enum (matches MLCVCameraID)
typedef enum {
    CVCameraID_ColorCamera = 0
} CVCameraID;

// CV Camera pose data structure
typedef struct CVCameraPose {
    // Rotation as quaternion (x, y, z, w)
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float rotation_w;
    // Position (x, y, z)
    float position_x;
    float position_y;
    float position_z;
    // Timestamp used for this pose
    int64_t timestampNs;
    // Result code from the API call
    int32_t resultCode;
} CVCameraPose;

// Initialize CV Camera tracking
// head_tracking_handle: Handle from MLHeadTrackingUnity_GetHandle() - REQUIRED
// Returns true on success
bool MLCVCameraUnity_Init(uint64_t head_tracking_handle);

// Get the camera pose
// timestamp_ns: The camera frame timestamp or 0 for current time
// camera_id: Which camera (typically CVCameraID_ColorCamera)
// out_pose: Output pose data
// Returns true if pose was successfully retrieved
bool MLCVCameraUnity_GetPose(
    int64_t timestamp_ns,
    CVCameraID camera_id,
    CVCameraPose* out_pose);

// Get current time from ML system
int64_t MLCVCameraUnity_GetCurrentTimeNs();

// Shutdown CV camera tracking (does NOT shutdown head tracking)
void MLCVCameraUnity_Shutdown();

// Check if initialized
bool MLCVCameraUnity_IsInitialized();

#ifdef __cplusplus
}
#endif