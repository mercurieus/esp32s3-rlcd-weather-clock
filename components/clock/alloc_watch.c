#include "alloc_watch.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "AllocWatch";

#define ALLOC_WATCH_MAGIC 0x41574331u   /* "AWC1" */

/* RTC_NOINIT survives a panic reboot, a watchdog and esp_restart(), and is
   lost on a power cycle. That is the right lifetime: the question this answers
   is always "what did the heap look like just before the restart that just
   happened", and after a power cycle there is no such restart to explain. */
static RTC_NOINIT_ATTR uint32_t         s_magic;
static RTC_NOINIT_ATTR AllocWatchRecord s_live;

static AllocWatchRecord s_previous;
static bool             s_have_previous;

/* Runs in whatever context the failed allocation ran in. That can be an ISR,
   and it can be with the flash cache disabled, so this does nothing but store
   its arguments. heap_caps_get_free_size() and heap_caps_get_largest_free_block()
   are not IRAM-safe - calling either here would turn an out-of-memory report
   into a crash of its own, in the one code path that must not crash. The heap
   numbers come from AllocWatch_Snapshot() in task context instead.

   The function-name argument is deliberately dropped: it is a .rodata pointer,
   and a record read back after a firmware update would resolve it against a
   different binary. The size and caps survive that; a stale pointer does not. */
static void IRAM_ATTR alloc_failed_hook(size_t size, uint32_t caps, const char *fn)
{
    (void)fn;
    s_live.fail_count++;
    s_live.fail_size = (uint32_t)size;
    s_live.fail_caps = caps;
    s_magic = ALLOC_WATCH_MAGIC;
}

void AllocWatch_Init(void)
{
    if (s_magic == ALLOC_WATCH_MAGIC) {
        s_previous = s_live;
        s_have_previous = true;
    }
    memset(&s_live, 0, sizeof(s_live));
    s_magic = ALLOC_WATCH_MAGIC;

    esp_err_t err = heap_caps_register_failed_alloc_callback(alloc_failed_hook);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not register the allocation-failure hook: %s",
                 esp_err_to_name(err));
    }
}

bool AllocWatch_PreviousBoot(AllocWatchRecord *out)
{
    if (!s_have_previous) {
        return false;
    }
    if (out) {
        *out = s_previous;
    }
    return true;
}

bool AllocWatch_PreviousBootFailed(void)
{
    return s_have_previous && s_previous.fail_count > 0;
}

void AllocWatch_Snapshot(AllocWatchTag tag)
{
    s_live.snap_tag      = (uint32_t)tag;
    s_live.snap_free     = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_live.snap_largest  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    s_live.snap_min_ever = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    s_live.snap_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    s_magic = ALLOC_WATCH_MAGIC;
}

const char *AllocWatch_TagName(uint32_t tag)
{
    switch (tag) {
    case ALLOC_WATCH_TAG_WIFI_START: return "wifi-start";
    case ALLOC_WATCH_TAG_FETCH:      return "fetch";
    case ALLOC_WATCH_TAG_SYNC_DONE:  return "sync-done";
    default:                         return "none";
    }
}
