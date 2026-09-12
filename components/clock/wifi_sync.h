#pragma once

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WifiSync_ConnectedCallback)(void *ctx);

/* What the radio is actually doing, as opposed to whether the last sync
   worked. The distinction matters on the panel: this board powers the radio up
   only for a sync, so an aerial glyph shown around the clock would be claiming
   a connection that does not exist for all but a few seconds a day. */
typedef enum {
    WIFI_LINK_OFF = 0,      /* radio down - the normal resting state here */
    WIFI_LINK_CONNECTING,   /* radio up, associating, no address yet */
    WIFI_LINK_CONNECTED,    /* associated and holding an IP */
} WifiLinkState;

WifiLinkState WifiSync_LinkState(void);

/* Called on the caller's own task, always - never from the Wi-Fi event loop,
   whose stack is far too small to repaint a panel. The transitions therefore
   land at the points WifiSync_SyncTimeOnce() passes through: bringing the
   radio up, getting an address, and shutting the radio down. The middle one
   still covers several seconds of SNTP and weather traffic, which is the
   window where "connected" is worth showing. */
void WifiSync_SetLinkObserver(void (*observer)(WifiLinkState state));

void WifiSync_Init(void);
/* On success out_time receives the current time in UTC, not local time.
   Use ClockTime_UtcToLocal to render it. */
bool WifiSync_SyncTimeOnce(struct tm *out_time, const char *ssid, const char *password, uint32_t timeout_ms,
                            WifiSync_ConnectedCallback on_connected, void *ctx);

#ifdef __cplusplus
}
#endif