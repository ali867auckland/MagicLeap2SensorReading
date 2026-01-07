#include "mlperception_service.h"

#include <atomic>
#include <mutex>

#include <android/log.h>
#include <ml_perception.h>

#define LOG_TAG "ML2RAW_NATIVE"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::mutex g_mtx;
static std::atomic<int> g_ref{0};
static bool g_started = false;

bool MLPerceptionService_Startup() {
  std::lock_guard<std::mutex> lk(g_mtx);

  if (g_started) {
    g_ref.fetch_add(1);
    return true;
  }

  MLPerceptionSettings settings{};
  MLResult r = MLPerceptionInitSettings(&settings);
  if (r != MLResult_Ok) {
    ALOGE("MLPerceptionInitSettings failed: %d", (int)r);
    return false;
  }

  r = MLPerceptionStartup(&settings);
  if (r != MLResult_Ok) {
    ALOGE("MLPerceptionStartup failed: %d", (int)r);
    return false;
  }

  g_started = true;
  g_ref.store(1);
  ALOGI("Perception started");
  return true;
}

void MLPerceptionService_Shutdown() {
  std::lock_guard<std::mutex> lk(g_mtx);

  if (!g_started) return;

  int ref = g_ref.load();
  if (ref > 1) {
    g_ref.store(ref - 1);
    return;
  }

  MLResult r = MLPerceptionShutdown();
  if (r != MLResult_Ok) {
    ALOGE("MLPerceptionShutdown failed: %d", (int)r);
  } else {
    ALOGI("Perception shutdown");
  }

  g_started = false;
  g_ref.store(0);
}

bool MLPerceptionService_IsStarted() {
  return g_started;
}
