#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts Perception (ref-counted). Safe to call from any sensor init.
bool MLPerceptionService_Startup();

// Starts Perception and waits until MLPerceptionGetSnapshot() succeeds (or timeout).
// This avoids "Perception system not started" races.
bool MLPerceptionService_StartupAndWait(uint32_t timeoutMs);

// Releases one ref; shuts Perception down when ref reaches 0.
void MLPerceptionService_Shutdown();

// Simple status flag (best-effort: true after successful Startup).
bool MLPerceptionService_IsStarted();

#ifdef __cplusplus
}
#endif
