#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Gaze behavior enum (matches MLGazeRecognitionBehavior)
typedef enum GazeBehavior {
    GazeBehavior_Unknown = 0,
    GazeBehavior_EyesClosed = 1,
    GazeBehavior_Blink = 2,
    GazeBehavior_Fixation = 3,
    GazeBehavior_Pursuit = 4,
    GazeBehavior_Saccade = 5,
    GazeBehavior_BlinkLeft = 6,
    GazeBehavior_BlinkRight = 7
} GazeBehavior;

// Gaze recognition state data
typedef struct GazeRecognitionData {
    int64_t timestampNs;
    
    // Current behavior
    int32_t behavior;  // GazeBehavior enum
    
    // Eye-in-skull position (normalized)
    float eye_left_x, eye_left_y;
    float eye_right_x, eye_right_y;
    
    // Behavior metadata
    float onset_s;           // When current behavior started
    float duration_s;        // How long behavior has lasted
    float velocity_degps;    // Eye velocity (saccade/pursuit only, NaN otherwise)
    float amplitude_deg;     // Eye displacement (saccade/pursuit only)
    float direction_radial;  // Direction in degrees 0-360 (saccade/pursuit only)
    
    // Error state (0 = none)
    int32_t error;
} GazeRecognitionData;

// Initialize gaze recognition
bool MLGazeRecognitionUnity_Init(void);

// Get latest gaze recognition data
bool MLGazeRecognitionUnity_GetLatest(GazeRecognitionData* out_data);

// Check if initialized
bool MLGazeRecognitionUnity_IsInitialized(void);

// Get sample count
uint64_t MLGazeRecognitionUnity_GetSampleCount(void);

// Shutdown
void MLGazeRecognitionUnity_Shutdown(void);

#ifdef __cplusplus
}
#endif