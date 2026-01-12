#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Head tracking status enum (matches MLHeadTrackingStatus)
typedef enum {
    HeadTrackingStatus_Invalid = 0,
    HeadTrackingStatus_Initializing = 1,
    HeadTrackingStatus_Relocalizing = 2,
    HeadTrackingStatus_Valid = 100
} HeadTrackingStatus;

// Head tracking error flags (matches MLHeadTrackingErrorFlag) - BITMASK
typedef enum {
    HeadTrackingErrorFlag_None = 0,
    HeadTrackingErrorFlag_Unknown = 1 << 0,
    HeadTrackingErrorFlag_NotEnoughFeatures = 1 << 1,
    HeadTrackingErrorFlag_LowLight = 1 << 2,
    HeadTrackingErrorFlag_ExcessiveMotion = 1 << 3
} HeadTrackingErrorFlag;

// Head tracking map events (matches MLHeadTrackingMapEvent) - BITMASK
typedef enum {
    HeadTrackingMapEvent_Lost = 1 << 0,
    HeadTrackingMapEvent_Recovered = 1 << 1,
    HeadTrackingMapEvent_RecoveryFailed = 1 << 2,
    HeadTrackingMapEvent_NewSession = 1 << 3
} HeadTrackingMapEvent;

// Head pose data structure
typedef struct HeadPoseData {
    // Rotation as quaternion (x, y, z, w)
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float rotation_w;
    // Position (x, y, z) in meters
    float position_x;
    float position_y;
    float position_z;
    // Timestamp
    int64_t timestampNs;
    // Tracking state (MLHeadTrackingStateEx fields)
    uint32_t status;        // HeadTrackingStatus enum value
    float confidence;       // 0.0 - 1.0
    uint32_t errorFlags;    // Bitmask of HeadTrackingErrorFlag
    // Map events (bitmask, not single value)
    uint64_t mapEventsMask; // Bitmask of HeadTrackingMapEvent
    bool hasMapEvent;
    // Result code
    int32_t resultCode;
} HeadPoseData;

// Initialize head tracking
// Returns true on success
bool MLHeadTrackingUnity_Init();

// Get the current head pose
// out_pose: Output pose data
// Returns true if pose was successfully retrieved
bool MLHeadTrackingUnity_GetPose(HeadPoseData* out_pose);

// Get the head tracking handle (for use by CV camera)
// Returns the MLHandle or 0 if not initialized
uint64_t MLHeadTrackingUnity_GetHandle();

// Check if initialized
bool MLHeadTrackingUnity_IsInitialized();

// Shutdown and release resources
void MLHeadTrackingUnity_Shutdown();

#ifdef __cplusplus
}
#endif