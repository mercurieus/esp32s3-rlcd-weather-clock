#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Records why the last restart ran out of memory, across the restart itself.

   The panic that reboots this board is IDF's: phy_track_pll_init() wraps
   esp_timer_create() in ESP_ERROR_CHECK, so an ESP_ERR_NO_MEM there aborts.
   The core dump names that call precisely but says nothing about the heap -
   it stores task stacks only - so the one thing needed to explain the failure
   is the one thing not captured. This fills that gap. */

typedef enum {
    ALLOC_WATCH_TAG_NONE = 0,
    ALLOC_WATCH_TAG_WIFI_START,
    ALLOC_WATCH_TAG_FETCH,
    ALLOC_WATCH_TAG_SYNC_DONE,
} AllocWatchTag;

typedef struct {
    uint32_t fail_count;     /* failed allocations seen during that run */
    uint32_t fail_size;      /* bytes the last failed allocation asked for */
    uint32_t fail_caps;      /* MALLOC_CAP_* bits it asked for */
    uint32_t snap_tag;       /* AllocWatchTag: what was about to happen */
    uint32_t snap_free;      /* free internal DRAM at that moment */
    uint32_t snap_largest;   /* largest free internal block */
    uint32_t snap_min_ever;  /* internal low-water mark for the whole run */
    uint32_t snap_uptime_s;
} AllocWatchRecord;

/* Call once, early, before anything that might allocate heavily. Rescues the
   previous run's record and starts a fresh one. */
void AllocWatch_Init(void);

/* The record from the run before this one, if there was one. */
bool AllocWatch_PreviousBoot(AllocWatchRecord *out);

/* True when that previous run recorded a failed allocation - i.e. the restart
   that followed is explained by memory rather than by a host or a brownout. */
bool AllocWatch_PreviousBootFailed(void);

/* Take a heap reading from task context. Do this immediately before anything
   that could exhaust memory: if the restart happens inside it, this is the
   last good look at the heap. */
void AllocWatch_Snapshot(AllocWatchTag tag);

const char *AllocWatch_TagName(uint32_t tag);

#ifdef __cplusplus
}
#endif
