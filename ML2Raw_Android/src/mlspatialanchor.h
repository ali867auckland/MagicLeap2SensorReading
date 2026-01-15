#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AnchorQuality_Low = 0,
    AnchorQuality_Medium = 1,
    AnchorQuality_High = 2
} AnchorQuality;

typedef struct AnchorCreationResult {
    bool success;
    uint64_t anchorId_data0;
    uint64_t anchorId_data1;
    int32_t resultCode;
} AnchorCreationResult;

typedef struct AnchorPoseData {
    uint64_t anchorId_data0;
    uint64_t anchorId_data1;
    
    float rotation_x, rotation_y, rotation_z, rotation_w;
    float position_x, position_y, position_z;
    
    uint32_t quality;
    uint8_t frameUid[16];
    int64_t timestampNs;
    int32_t resultCode;
} AnchorPoseData;

bool MLSpatialAnchorUnity_Init(void);

AnchorCreationResult MLSpatialAnchorUnity_CreateAnchor(
    float rotation_x, float rotation_y, float rotation_z, float rotation_w,
    float position_x, float position_y, float position_z);

bool MLSpatialAnchorUnity_GetAnchorPose(
    uint64_t anchor_id_0, uint64_t anchor_id_1,
    AnchorPoseData* out_pose);

bool MLSpatialAnchorUnity_GetAllAnchors(
    AnchorPoseData* out_poses, int32_t max_count, int32_t* out_count);

float MLSpatialAnchorUnity_GetDistanceToNearestAnchor(
    float pos_x, float pos_y, float pos_z);

bool MLSpatialAnchorUnity_DeleteAnchor(
    uint64_t anchor_id_0, uint64_t anchor_id_1);

int32_t MLSpatialAnchorUnity_GetAnchorCount(void);

void MLSpatialAnchorUnity_SetAutoCreate(bool enabled, float min_distance, uint32_t max_anchors);

void MLSpatialAnchorUnity_Update(void);

bool MLSpatialAnchorUnity_IsInitialized(void);

void MLSpatialAnchorUnity_Shutdown(void);

#ifdef __cplusplus
}
#endif