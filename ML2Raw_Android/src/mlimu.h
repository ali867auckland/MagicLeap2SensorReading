#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// IMU data structure - accelerometer + gyroscope combined
typedef struct IMUData {
    // Accelerometer (m/s²)
    float accel_x;
    float accel_y;
    float accel_z;
    int64_t accel_timestamp_ns;
    
    // Gyroscope (rad/s)
    float gyro_x;
    float gyro_y;
    float gyro_z;
    int64_t gyro_timestamp_ns;
    
    // Flags
    int32_t has_accel;  // 1 if accel data valid
    int32_t has_gyro;   // 1 if gyro data valid
} IMUData;

// Initialize IMU sensors
// sample_rate_hz: desired sampling rate (e.g., 100, 200, 500)
bool MLIMUUnity_Init(int32_t sample_rate_hz);

// Check if initialized
bool MLIMUUnity_IsInitialized();

// Get latest IMU data (non-blocking)
// Returns true if new data available
bool MLIMUUnity_TryGetLatest(IMUData* out_data);

// Get buffered IMU data (for high-frequency capture)
// out_data: array to fill
// max_count: size of array
// out_count: number of samples written
// Returns true if any data retrieved
bool MLIMUUnity_GetBuffered(IMUData* out_data, int32_t max_count, int32_t* out_count);

// Get sample counts
uint64_t MLIMUUnity_GetAccelCount();
uint64_t MLIMUUnity_GetGyroCount();

// Shutdown
void MLIMUUnity_Shutdown();

#ifdef __cplusplus
}
#endif