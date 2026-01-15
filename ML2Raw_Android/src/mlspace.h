#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Space localization status (matches MLSpaceLocalizationStatus)
typedef enum {
    SpaceLocalizationStatus_NotLocalized = 0,
    SpaceLocalizationStatus_Localized = 1,
    SpaceLocalizationStatus_LocalizationPending = 2,
    SpaceLocalizationStatus_SleepingBeforeRetry = 3
} SpaceLocalizationStatus;

// Space localization confidence (matches MLSpaceLocalizationConfidence)
typedef enum {
    SpaceLocalizationConfidence_Poor = 0,
    SpaceLocalizationConfidence_Fair = 1,
    SpaceLocalizationConfidence_Good = 2,
    SpaceLocalizationConfidence_Excellent = 3
} SpaceLocalizationConfidence;

// Space localization error flags (matches MLSpaceLocalizationErrorFlag) - BITMASK
typedef enum {
    SpaceLocalizationErrorFlag_None = 0,
    SpaceLocalizationErrorFlag_Unknown = 1 << 0,
    SpaceLocalizationErrorFlag_OutOfMappedArea = 1 << 1,
    SpaceLocalizationErrorFlag_LowFeatureCount = 1 << 2,
    SpaceLocalizationErrorFlag_ExcessiveMotion = 1 << 3,
    SpaceLocalizationErrorFlag_LowLight = 1 << 4,
    SpaceLocalizationErrorFlag_HeadposeFailure = 1 << 5,
    SpaceLocalizationErrorFlag_AlgorithmFailure = 1 << 6
} SpaceLocalizationErrorFlag;

// Space type (matches MLSpaceType)
typedef enum {
    SpaceType_OnDevice = 0,
    SpaceType_ARCloud = 1
} SpaceType;

// Space localization data structure
typedef struct SpaceLocalizationData {
    // Localization state
    uint32_t status;           // SpaceLocalizationStatus enum
    uint32_t confidence;       // SpaceLocalizationConfidence enum
    uint32_t errorFlags;       // Bitmask of SpaceLocalizationErrorFlag
    uint32_t spaceType;        // SpaceType enum
    
    // Space identification
    uint64_t spaceId_data0;    // MLUUID first 64 bits
    uint64_t spaceId_data1;    // MLUUID second 64 bits
    char spaceName[64];        // Space name (MLSpace_MaxSpaceNameLength)
    
    // Timing
    int64_t timestampNs;
    
    // Target space origin frame UID (if localized)
    uint8_t targetSpaceOrigin[16]; // MLCoordinateFrameUID (16 bytes)
    
    // Result code
    int32_t resultCode;
} SpaceLocalizationData;

// Space info for space list
typedef struct SpaceInfo {
    uint64_t spaceId_data0;
    uint64_t spaceId_data1;
    char spaceName[64];
    uint32_t spaceType;        // SpaceType enum
    int64_t timestampNs;
} SpaceInfo;

// Initialize Space manager
// Returns true on success
bool MLSpaceUnity_Init();

// Get current localization status
// out_data: Output localization data
// Returns true if data was successfully retrieved
bool MLSpaceUnity_GetLocalizationStatus(SpaceLocalizationData* out_data);

// Get list of available spaces
// out_spaces: Array to hold space info (caller allocated)
// max_spaces: Maximum number of spaces to return
// out_count: Actual number of spaces returned
// Returns true if successful
bool MLSpaceUnity_GetSpaceList(SpaceInfo* out_spaces, int32_t max_spaces, int32_t* out_count);

// Request localization to a specific space
// space_id_data0, space_id_data1: Space UUID
// Returns true if request was submitted
bool MLSpaceUnity_RequestLocalization(uint64_t space_id_data0, uint64_t space_id_data1);

// Check if initialized
bool MLSpaceUnity_IsInitialized();

// Shutdown and release resources
void MLSpaceUnity_Shutdown();

#ifdef __cplusplus
}
#endif