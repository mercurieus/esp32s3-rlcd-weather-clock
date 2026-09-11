#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Runs back-to-back Wi-Fi bring-up/teardown cycles and reports how internal
   DRAM moves across each one.

   It exists because the failure it hunts - esp_timer_create() returning
   ESP_ERR_NO_MEM inside phy_track_pll_init(), which ESP-IDF turns into an
   abort and a reboot - takes hours to reproduce at the normal 30-minute
   refresh cadence. At a few seconds per cycle it takes minutes instead.

   Two passes run: one without the weather fetch, one with. A leak that only
   appears in the second is in the HTTP/TLS path, not in the radio.

   Compiled out entirely unless CONFIG_CLOCK_WIFI_STRESS is set, and never on
   by default - it holds boot for a minute or two and hammers the AP. */
void WifiStress_Run(void);

#ifdef __cplusplus
}
#endif
