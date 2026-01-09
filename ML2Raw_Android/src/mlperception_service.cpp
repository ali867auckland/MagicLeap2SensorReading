#include "mlperception_service.h"

#include <atomic>
#include <mutex>
#include <stdint.h>

#include <android/log.h>
#include <ml_perception.h>

#include <time.h>
#include <unistd.h> // usleep

#define LOG_TAG "ML2RAW_NATIVE"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::mutex g_mtx;
static std::atomic<int> g_ref{0};
static bool g_started = false;

static uint64_t NowMonotonicMs() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static bool WaitPerceptionReady(uint32_t timeoutMs) {
  const uint64_t t0 = NowMonotonicMs();
  MLSnapshot* snap = nullptr;

  while (true) {
    MLResult r = MLPerceptionGetSnapshot(&snap);
    if (r == MLResult_Ok && snap) {
      MLPerceptionReleaseSnapshot(snap);
      return true;
    }

    // If not started yet / racing, just retry briefly.
    // (Don’t spam logs every iteration.)
    if (NowMonotonicMs() - t0 >= (uint64_t)timeoutMs) {
      ALOGE("Perception not ready after %u ms (last MLPerceptionGetSnapshot r=%d)", timeoutMs, (int)r);
      return false;
    }

    // Sleep ~10ms
    usleep(10 * 1000);
  }
}

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

bool MLPerceptionService_StartupAndWait(uint32_t timeoutMs) {
  // Start/ref-count first
  if (!MLPerceptionService_Startup()) return false;

  // Then wait until snapshots work (prevents MLHeadTrackingCreate() race)
  if (!WaitPerceptionReady(timeoutMs)) {
    // If we started it in this call path, we should release our ref.
    // (Refcounted, so safe.)
    MLPerceptionService_Shutdown();
    return false;
  }

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
