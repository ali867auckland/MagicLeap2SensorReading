#include "mlimu.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

#include <android/sensor.h>
#include <android/looper.h>
#include <android/log.h>

#define LOG_TAG "MLIMUUnity"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

// Ring buffer size for high-frequency IMU data
static constexpr size_t BUFFER_SIZE = 2048;

// State
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_running{false};
static std::thread g_thread;
static std::mutex g_mutex;

// Android sensor handles
static ASensorManager* g_sensorManager = nullptr;
static const ASensor* g_accelSensor = nullptr;
static const ASensor* g_gyroSensor = nullptr;
static ASensorEventQueue* g_eventQueue = nullptr;
static ALooper* g_looper = nullptr;

// Latest data
static IMUData g_latestData;
static std::atomic<bool> g_hasNewData{false};

// Ring buffer for high-frequency data
static std::vector<IMUData> g_buffer;
static size_t g_bufferHead = 0;
static size_t g_bufferTail = 0;
static size_t g_bufferCount = 0;

// Counters
static std::atomic<uint64_t> g_accelCount{0};
static std::atomic<uint64_t> g_gyroCount{0};

// Sample rate
static int32_t g_sampleRateHz = 200;

// Sensor callback (called from looper)
static int SensorCallback(int fd, int events, void* data) {
    (void)fd;
    (void)events;
    (void)data;
    
    ASensorEvent event;
    
    while (ASensorEventQueue_getEvents(g_eventQueue, &event, 1) > 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        
        if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            // Use data array - most portable across NDK versions
            g_latestData.accel_x = event.data[0];
            g_latestData.accel_y = event.data[1];
            g_latestData.accel_z = event.data[2];
            g_latestData.accel_timestamp_ns = event.timestamp;
            g_latestData.has_accel = 1;
            g_accelCount.fetch_add(1);
        }
        else if (event.type == ASENSOR_TYPE_GYROSCOPE) {
            // Use data array - most portable across NDK versions
            g_latestData.gyro_x = event.data[0];
            g_latestData.gyro_y = event.data[1];
            g_latestData.gyro_z = event.data[2];
            g_latestData.gyro_timestamp_ns = event.timestamp;
            g_latestData.has_gyro = 1;
            g_gyroCount.fetch_add(1);
        }
        
        // Store in ring buffer when we have both accel and gyro
        if (g_latestData.has_accel && g_latestData.has_gyro) {
            g_buffer[g_bufferHead] = g_latestData;
            g_bufferHead = (g_bufferHead + 1) % BUFFER_SIZE;
            
            if (g_bufferCount < BUFFER_SIZE) {
                g_bufferCount++;
            } else {
                // Buffer full, advance tail
                g_bufferTail = (g_bufferTail + 1) % BUFFER_SIZE;
            }
            
            g_hasNewData.store(true);
        }
    }
    
    return 1; // Continue receiving events
}

// Sensor polling thread
static void SensorLoop() {
    LOGI("Sensor thread started");
    
    // Create looper for this thread
    g_looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    if (!g_looper) {
        LOGE("Failed to prepare looper");
        return;
    }
    
    // Create event queue
    g_eventQueue = ASensorManager_createEventQueue(
        g_sensorManager,
        g_looper,
        ALOOPER_POLL_CALLBACK,
        SensorCallback,
        nullptr
    );
    
    if (!g_eventQueue) {
        LOGE("Failed to create sensor event queue");
        return;
    }
    
    // Calculate sample period in microseconds
    int32_t samplePeriodUs = 1000000 / g_sampleRateHz;
    
    // Enable accelerometer
    if (g_accelSensor) {
        if (ASensorEventQueue_enableSensor(g_eventQueue, g_accelSensor) < 0) {
            LOGE("Failed to enable accelerometer");
        } else {
            ASensorEventQueue_setEventRate(g_eventQueue, g_accelSensor, samplePeriodUs);
            LOGI("Accelerometer enabled at %d Hz", g_sampleRateHz);
        }
    }
    
    // Enable gyroscope
    if (g_gyroSensor) {
        if (ASensorEventQueue_enableSensor(g_eventQueue, g_gyroSensor) < 0) {
            LOGE("Failed to enable gyroscope");
        } else {
            ASensorEventQueue_setEventRate(g_eventQueue, g_gyroSensor, samplePeriodUs);
            LOGI("Gyroscope enabled at %d Hz", g_sampleRateHz);
        }
    }
    
    // Poll loop
    while (g_running.load()) {
        // Poll with timeout (10ms) - use pollOnce instead of deprecated pollAll
        ALooper_pollOnce(10, nullptr, nullptr, nullptr);
    }
    
    // Cleanup
    if (g_accelSensor) {
        ASensorEventQueue_disableSensor(g_eventQueue, g_accelSensor);
    }
    if (g_gyroSensor) {
        ASensorEventQueue_disableSensor(g_eventQueue, g_gyroSensor);
    }
    
    ASensorManager_destroyEventQueue(g_sensorManager, g_eventQueue);
    g_eventQueue = nullptr;
    
    LOGI("Sensor thread exiting");
}

bool MLIMUUnity_Init(int32_t sample_rate_hz) {
    if (g_initialized.load()) {
        LOGI("Already initialized");
        return true;
    }
    
    g_sampleRateHz = sample_rate_hz > 0 ? sample_rate_hz : 200;
    
    LOGI("Initializing IMU at %d Hz", g_sampleRateHz);
    
    // Get sensor manager
    g_sensorManager = ASensorManager_getInstanceForPackage(nullptr);
    
    if (!g_sensorManager) {
        LOGE("Failed to get sensor manager");
        return false;
    }
    
    // Get accelerometer
    g_accelSensor = ASensorManager_getDefaultSensor(g_sensorManager, ASENSOR_TYPE_ACCELEROMETER);
    if (!g_accelSensor) {
        LOGW("Accelerometer not available");
    } else {
        LOGI("Accelerometer: %s", ASensor_getName(g_accelSensor));
    }
    
    // Get gyroscope
    g_gyroSensor = ASensorManager_getDefaultSensor(g_sensorManager, ASENSOR_TYPE_GYROSCOPE);
    if (!g_gyroSensor) {
        LOGW("Gyroscope not available");
    } else {
        LOGI("Gyroscope: %s", ASensor_getName(g_gyroSensor));
    }
    
    if (!g_accelSensor && !g_gyroSensor) {
        LOGE("No IMU sensors available");
        return false;
    }
    
    // Initialize buffer
    g_buffer.resize(BUFFER_SIZE);
    g_bufferHead = 0;
    g_bufferTail = 0;
    g_bufferCount = 0;
    
    // Clear latest data
    memset(&g_latestData, 0, sizeof(g_latestData));
    
    g_initialized.store(true);
    g_running.store(true);
    g_thread = std::thread(SensorLoop);
    
    LOGI("IMU initialized");
    return true;
}

bool MLIMUUnity_IsInitialized() {
    return g_initialized.load();
}

bool MLIMUUnity_TryGetLatest(IMUData* out_data) {
    if (!out_data || !g_initialized.load()) {
        return false;
    }
    
    if (!g_hasNewData.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    *out_data = g_latestData;
    g_hasNewData.store(false);
    
    return true;
}

bool MLIMUUnity_GetBuffered(IMUData* out_data, int32_t max_count, int32_t* out_count) {
    if (!out_data || !out_count || max_count <= 0 || !g_initialized.load()) {
        if (out_count) *out_count = 0;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    int32_t count = 0;
    while (g_bufferCount > 0 && count < max_count) {
        out_data[count] = g_buffer[g_bufferTail];
        g_bufferTail = (g_bufferTail + 1) % BUFFER_SIZE;
        g_bufferCount--;
        count++;
    }
    
    *out_count = count;
    return count > 0;
}

uint64_t MLIMUUnity_GetAccelCount() {
    return g_accelCount.load();
}

uint64_t MLIMUUnity_GetGyroCount() {
    return g_gyroCount.load();
}

void MLIMUUnity_Shutdown() {
    LOGI("Shutting down...");
    
    g_running.store(false);
    
    if (g_thread.joinable()) {
        g_thread.join();
    }
    
    g_sensorManager = nullptr;
    g_accelSensor = nullptr;
    g_gyroSensor = nullptr;
    
    g_buffer.clear();
    g_bufferHead = 0;
    g_bufferTail = 0;
    g_bufferCount = 0;
    
    g_accelCount.store(0);
    g_gyroCount.store(0);
    
    g_initialized.store(false);
    
    LOGI("Shutdown complete");
}