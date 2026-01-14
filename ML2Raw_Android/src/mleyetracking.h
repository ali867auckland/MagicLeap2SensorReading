#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Eye tracking state data
typedef struct EyeTrackingData {
    int64_t timestampNs;
    
    // Confidence values (0.0 - 1.0)
    float vergence_confidence;
    float left_center_confidence;
    float right_center_confidence;
    
    // Blink state
    int32_t left_blink;   // 1 = blinking
    int32_t right_blink;
    
    // Eye openness (0.0 = closed, 1.0 = fully open)
    float left_eye_openness;
    float right_eye_openness;
    
    // Gaze direction (from head tracking pose query)
    float left_gaze_x, left_gaze_y, left_gaze_z;
    float right_gaze_x, right_gaze_y, right_gaze_z;
    
    // Vergence point (where eyes converge)
    float vergence_x, vergence_y, vergence_z;
    
    // Position of eye centers
    float left_pos_x, left_pos_y, left_pos_z;
    float right_pos_x, right_pos_y, right_pos_z;
    
    // Error state (0 = none)
    int32_t error;
    
    // Pose validity flags
    int32_t vergence_valid;
    int32_t left_valid;
    int32_t right_valid;
} EyeTrackingData;

// Initialize eye tracking
bool MLEyeTrackingUnity_Init(void);

// Get latest eye tracking data
bool MLEyeTrackingUnity_GetLatest(EyeTrackingData* out_data);

// Check if initialized
bool MLEyeTrackingUnity_IsInitialized(void);

// Get sample count
uint64_t MLEyeTrackingUnity_GetSampleCount(void);

// Shutdown
void MLEyeTrackingUnity_Shutdown(void);

#ifdef __cplusplus
}
#endif