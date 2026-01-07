#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts Perception (ref-counted). Safe to call from any sensor init.
bool MLPerceptionService_Startup();

// Releases one ref; shuts Perception down when ref reaches 0.
void MLPerceptionService_Shutdown();

// Simple status flag
bool MLPerceptionService_IsStarted();

#ifdef __cplusplus
}
#endif
