#include "mlspace.h"

#include <atomic>
#include <mutex>
#include <cstring>
#include <time.h>

#include <android/log.h>
#include <ml_space.h>

#define LOG_TAG "MLSpaceUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

static bool g_debug = true;

static std::mutex g_lock;
static std::atomic<bool> g_initialized{false};

static MLHandle g_spaceManagerHandle = ML_INVALID_HANDLE;

static const char* ResultToString(MLResult r) {
    switch (r) {
        case MLResult_Ok: return "Ok";
        case MLResult_InvalidParam: return "InvalidParam";
        case MLResult_UnspecifiedFailure: return "UnspecifiedFailure";
        case MLResult_PermissionDenied: return "PermissionDenied";
        case MLResult_Timeout: return "Timeout";
        default: return "Unknown";
    }
}

static int64_t NowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

bool MLSpaceUnity_Init(void) {
    std::lock_guard<std::mutex> guard(g_lock);
    
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }

    MLSpaceManagerSettings settings;
    MLSpaceManagerSettingsInit(&settings);

    MLResult r = MLSpaceManagerCreate(&settings, &g_spaceManagerHandle);
    if (r != MLResult_Ok || g_spaceManagerHandle == ML_INVALID_HANDLE) {
        LOGE("MLSpaceManagerCreate FAILED r=%d (%s)", (int)r, ResultToString(r));
        g_spaceManagerHandle = ML_INVALID_HANDLE;
        return false;
    }

    if (g_debug) {
        LOGI("MLSpaceManagerCreate OK handle=%llu", (unsigned long long)g_spaceManagerHandle);
    }

    g_initialized.store(true);
    LOGI("Space manager initialized successfully");
    return true;
}

bool MLSpaceUnity_GetLocalizationStatus(SpaceLocalizationData* out_data) {
    if (!out_data) return false;

    std::memset(out_data, 0, sizeof(SpaceLocalizationData));

    if (!g_initialized.load()) {
        out_data->resultCode = (int32_t)MLResult_UnspecifiedFailure;
        return false;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    MLSpaceLocalizationResult locResult;
    MLSpaceLocalizationResultInit(&locResult);

    MLResult r = MLSpaceGetLocalizationResult(g_spaceManagerHandle, &locResult);

    out_data->timestampNs = NowNs();
    out_data->resultCode = (int32_t)r;

    if (r != MLResult_Ok) {
        if (g_debug && r != MLResult_Timeout) {
            LOGW("MLSpaceGetLocalizationResult failed r=%d (%s)", (int)r, ResultToString(r));
        }
        return false;
    }

    // MLSpaceLocalizationResult has different fields than the old deprecated version
    // Access only the fields that actually exist in the SDK
    // Based on SDK: MLSpaceLocalizationResult has localization_status and space
    
    // Copy space information if available
    if (locResult.space.space_name[0] != '\0') {
        std::strncpy(out_data->spaceName, locResult.space.space_name, 63);
        out_data->spaceName[63] = '\0';
    }

    std::memcpy(&out_data->spaceId_data0, &locResult.space.space_id.data[0], 8);
    std::memcpy(&out_data->spaceId_data1, &locResult.space.space_id.data[8], 8);

    out_data->spaceType = (uint32_t)locResult.space.space_type;

    // Store localization status (newer API uses different field names)
    out_data->status = (uint32_t)locResult.localization_status;
    
    // Newer API doesn't provide confidence/error_flags directly
    // Set defaults
    out_data->confidence = 0;
    out_data->errorFlags = 0;

    // Copy target space origin (it's a struct, not a pointer)
    std::memcpy(out_data->targetSpaceOrigin, &locResult.target_space_origin, 16);

    if (g_debug) {
        LOGI("Localization: status=%d type=%d name=%s",
             out_data->status, out_data->spaceType, out_data->spaceName);
    }

    return true;
}

bool MLSpaceUnity_GetSpaceList(SpaceInfo* out_spaces, int32_t max_spaces, int32_t* out_count) {
    if (!out_spaces || !out_count || max_spaces <= 0) return false;

    *out_count = 0;

    if (!g_initialized.load()) {
        return false;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    MLSpaceQueryFilter filter;
    MLSpaceQueryFilterInit(&filter);

    MLSpaceList spaceList;
    MLSpaceListInit(&spaceList);

    MLResult r = MLSpaceGetSpaceList(g_spaceManagerHandle, &filter, &spaceList);
    if (r != MLResult_Ok) {
        LOGW("MLSpaceGetSpaceList failed r=%d (%s)", (int)r, ResultToString(r));
        return false;
    }

    if (spaceList.space_count == 0 || spaceList.spaces == nullptr) {
        MLSpaceReleaseSpaceList(g_spaceManagerHandle, &spaceList);
        return true;
    }

    int32_t count = (int32_t)spaceList.space_count;
    if (count > max_spaces) count = max_spaces;

    for (int32_t i = 0; i < count; i++) {
        const MLSpace& space = spaceList.spaces[i];

        std::memcpy(&out_spaces[i].spaceId_data0, &space.space_id.data[0], 8);
        std::memcpy(&out_spaces[i].spaceId_data1, &space.space_id.data[8], 8);
        
        if (space.space_name[0] != '\0') {
            std::strncpy(out_spaces[i].spaceName, space.space_name, 63);
            out_spaces[i].spaceName[63] = '\0';
        }
        
        out_spaces[i].spaceType = (uint32_t)space.space_type;
        out_spaces[i].timestampNs = NowNs();
    }

    *out_count = count;

    MLSpaceReleaseSpaceList(g_spaceManagerHandle, &spaceList);

    if (g_debug) {
        LOGI("Found %d spaces", count);
    }

    return true;
}

bool MLSpaceUnity_RequestLocalization(uint64_t space_id_data0, uint64_t space_id_data1) {
    if (!g_initialized.load()) {
        return false;
    }

    std::lock_guard<std::mutex> guard(g_lock);

    MLSpaceLocalizationInfo locInfo;
    MLSpaceLocalizationInfoInit(&locInfo);

    std::memcpy(&locInfo.space_id.data[0], &space_id_data0, 8);
    std::memcpy(&locInfo.space_id.data[8], &space_id_data1, 8);

    MLResult r = MLSpaceRequestLocalization(g_spaceManagerHandle, &locInfo);
    if (r != MLResult_Ok) {
        LOGE("MLSpaceRequestLocalization failed r=%d (%s)", (int)r, ResultToString(r));
        return false;
    }

    if (g_debug) {
        LOGI("Localization requested for space ID: %016llx%016llx", 
             (unsigned long long)space_id_data0, (unsigned long long)space_id_data1);
    }

    return true;
}

bool MLSpaceUnity_IsInitialized(void) {
    return g_initialized.load();
}

void MLSpaceUnity_Shutdown(void) {
    std::lock_guard<std::mutex> guard(g_lock);

    if (!g_initialized.load()) return;

    g_initialized.store(false);

    if (g_spaceManagerHandle != ML_INVALID_HANDLE) {
        MLResult r = MLSpaceManagerDestroy(g_spaceManagerHandle);
        if (g_debug) {
            LOGI("MLSpaceManagerDestroy r=%d (%s)", (int)r, ResultToString(r));
        }
        g_spaceManagerHandle = ML_INVALID_HANDLE;
    }

    LOGI("Space manager shutdown complete");
}