#pragma once

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WifiSync_ConnectedCallback)(void *ctx);

void WifiSync_Init(void);
/* On success out_time receives the current time in UTC, not local time.
   Use ClockTime_UtcToLocal to render it. */
bool WifiSync_SyncTimeOnce(struct tm *out_time, const char *ssid, const char *password, uint32_t timeout_ms,
                            WifiSync_ConnectedCallback on_connected, void *ctx);

#ifdef __cplusplus
}
#endif