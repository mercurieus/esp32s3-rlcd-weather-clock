#include "wifi_stress.h"

/* sdkconfig.h explicitly, and before the #if: without it CONFIG_CLOCK_WIFI_STRESS
   is simply undefined here, the preprocessor reads that as 0, and the whole file
   compiles to the empty stub with no warning. The build succeeds, the flash
   succeeds, and the test silently does not exist - which is exactly what
   happened the first time this ran. */
#include "sdkconfig.h"

#if CONFIG_CLOCK_WIFI_STRESS

#include "wifi_sync.h"
#include "weather.h"

#include "esp_heap_caps.h"
#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/priv/tcpip_priv.h"   /* LOCK_TCPIP_CORE, a no-op without core locking */

#include <time.h>

static const char *TAG = "WifiStress";

/* The heap trace resolves the leaked 208-byte block to tcp_alloc via pcb_new,
   but the stack stops at tcpip_thread: netconn_new posts a message and the
   allocation happens on lwIP's own thread, so the caller is not recoverable
   from a backtrace at any depth. What is recoverable is the state the PCB is
   sitting in, and that is what separates the remaining explanations - a close
   that never finished, a TIME_WAIT that outlives 2 x MSL, or a PCB orphaned
   off every list.

   Core locking is disabled in this build, so LOCK_TCPIP_CORE() is a no-op and
   this walk races the tcpip thread in principle. It is called only when the
   link is idle, and it is a diagnostic, not shipped behaviour. */
static const char *tcp_state_name(enum tcp_state st)
{
    switch (st) {
    case CLOSED:      return "CLOSED";
    case LISTEN:      return "LISTEN";
    case SYN_SENT:    return "SYN_SENT";
    case SYN_RCVD:    return "SYN_RCVD";
    case ESTABLISHED: return "ESTABLISHED";
    case FIN_WAIT_1:  return "FIN_WAIT_1";
    case FIN_WAIT_2:  return "FIN_WAIT_2";
    case CLOSE_WAIT:  return "CLOSE_WAIT";
    case CLOSING:     return "CLOSING";
    case LAST_ACK:    return "LAST_ACK";
    case TIME_WAIT:   return "TIME_WAIT";
    default:          return "?";
    }
}

static void dump_tcp_pcbs(const char *when)
{
    int active = 0, tw = 0, bound = 0;

    LOCK_TCPIP_CORE();
    ESP_LOGW(TAG, "---- TCP PCBs %s (tcp_ticks %lu) ----", when,
             (unsigned long)tcp_ticks);
    for (struct tcp_pcb *pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next) {
        ESP_LOGW(TAG, "  active %p %-11s local :%u remote :%u tmr %lu flags 0x%04x",
                 pcb, tcp_state_name(pcb->state), pcb->local_port,
                 pcb->remote_port, (unsigned long)pcb->tmr,
                 (unsigned)pcb->flags);
        active++;
    }
    for (struct tcp_pcb *pcb = tcp_tw_pcbs; pcb != NULL; pcb = pcb->next) {
        ESP_LOGW(TAG, "  tw     %p %-11s local :%u remote :%u tmr %lu",
                 pcb, tcp_state_name(pcb->state), pcb->local_port,
                 pcb->remote_port, (unsigned long)pcb->tmr);
        tw++;
    }
    for (struct tcp_pcb *pcb = tcp_bound_pcbs; pcb != NULL; pcb = pcb->next) {
        ESP_LOGW(TAG, "  bound  %p %-11s local :%u", pcb,
                 tcp_state_name(pcb->state), pcb->local_port);
        bound++;
    }
    UNLOCK_TCPIP_CORE();

    /* A count lower than the number of leaked blocks means the PCBs are on no
       list at all, which would make this a genuine orphan rather than a
       connection still working through its shutdown. */
    ESP_LOGW(TAG, "---- totals %s: active %d, time-wait %d, bound %d ----",
             when, active, tw, bound);
}

static WeatherDay s_days[4];
static WeatherNow s_now;

static void fetch_weather_cb(void *ctx)
{
    (void)ctx;
    Weather_Fetch(s_days, &s_now);
}

/* One pass of `cycles` bring-ups. with_fetch selects whether the weather
   request runs inside each cycle, which is what separates a leak in the radio
   path from one in HTTP/TLS. Returns the net change in free internal DRAM
   across the whole pass. */
static long run_pass(const char *label, int cycles, bool with_fetch)
{
    /* Settle first: the previous pass may have left the radio tearing down,
       and a figure taken mid-teardown reads as a leak that is not there. */
    vTaskDelay(pdMS_TO_TICKS(1000));

    const size_t start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGW(TAG, "== pass '%s': %d cycles, weather fetch %s, start %u B free ==",
             label, cycles, with_fetch ? "ON" : "OFF", (unsigned)start);

    for (int i = 1; i <= cycles; i++) {
        const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        struct tm utc;
        const bool ok = WifiSync_SyncTimeOnce(&utc, CONFIG_CLOCK_WIFI_SSID,
                                              CONFIG_CLOCK_WIFI_PASS, 15000,
                                              with_fetch ? fetch_weather_cb : NULL,
                                              NULL);

        vTaskDelay(pdMS_TO_TICKS(500));   /* let teardown finish before measuring */

        const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGW(TAG,
                 "%s %2d/%d ok=%d  free %u  cycle %+ld  cumulative %+ld  largest %u",
                 label, i, cycles, (int)ok, (unsigned)after,
                 (long)after - (long)before,
                 (long)after - (long)start,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }

    const size_t end = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const long net = (long)end - (long)start;
    ESP_LOGW(TAG, "== pass '%s' done: net %+ld B over %d cycles (%+ld B/cycle) ==",
             label, net, cycles, cycles ? net / cycles : 0);
    return net;
}

/* Repeated fetches inside ONE Wi-Fi session. The per-cycle variant cannot
   isolate the HTTP/TLS path: it pays the PHY bring-up each time, and the USB
   link has been observed dropping during a TLS handshake, which ends the run
   before enough cycles accumulate to see a trend. Holding the radio up
   removes both. */
static int s_fetch_count;

#if CONFIG_HEAP_TRACING_STANDALONE
/* Enough records to cover a dozen fetches without wrapping. Each is a couple
   of dozen bytes and they live in internal RAM, which is why this is behind a
   config option rather than always on. */
#define TRACE_RECORDS 400
static heap_trace_record_t s_trace[TRACE_RECORDS];
#endif

static void fetch_many_cb(void *ctx)
{
    (void)ctx;
    const size_t start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

#if CONFIG_HEAP_TRACING_STANDALONE
    /* HEAP_TRACE_LEAKS keeps only allocations that are still outstanding, so
       whatever the dump lists after the loop is what the fetch did not give
       back - with the call site, which totals alone cannot give. */
    ESP_ERROR_CHECK(heap_trace_init_standalone(s_trace, TRACE_RECORDS));
    ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));
#endif
    ESP_LOGW(TAG, "== pass 'fetch': %d fetches in one session, start %u B free ==",
             s_fetch_count, (unsigned)start);

    for (int i = 1; i <= s_fetch_count; i++) {
        const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const bool ok = Weather_Fetch(s_days, &s_now);
        const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        ESP_LOGW(TAG,
                 "fetch %2d/%d ok=%d  free %u  cycle %+ld  cumulative %+ld  largest %u",
                 i, s_fetch_count, (int)ok, (unsigned)after,
                 (long)after - (long)before,
                 (long)after - (long)start,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

        vTaskDelay(pdMS_TO_TICKS(300));
    }

#if CONFIG_HEAP_TRACING_STANDALONE
    /* Deliberately NOT stopping the trace here. HEAP_TRACE_LEAKS drops a
       record when it sees the matching free, so a tracer stopped before the
       settle cannot see the frees that happen during it and reports every
       TIME_WAIT PCB as a leak. That artifact cost a lot of time here: it
       looked like one 208-byte PCB leaked per fetch, at every settle length,
       which is exactly what an artifact looks like - the count never moved
       because the measurement had already ended. */
#endif

    const size_t end = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGW(TAG, "== pass 'fetch' done: net %+ld B over %d fetches (%+ld B/fetch) ==",
             (long)end - (long)start, s_fetch_count,
             s_fetch_count ? ((long)end - (long)start) / s_fetch_count : 0);

#if CONFIG_HEAP_TRACING_STANDALONE
    /* Longer than 2 x TCP_MSL, which is how long lwIP holds a closed
       connection in TIME_WAIT - CONFIG_LWIP_TCP_MSL is 60000, so 120s. The
       PCB dump either side of the wait is the ground truth: whatever the heap
       trace says, a PCB that is on none of lwIP's lists has been freed. */
    dump_tcp_pcbs("right after the fetch pass");
    ESP_LOGW(TAG, "settling 200s before the leak dump, past 2 x TCP_MSL (120s)");
    vTaskDelay(pdMS_TO_TICKS(200000));
    ESP_LOGW(TAG, "free after settle: %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    dump_tcp_pcbs("after the 200s settle");
    heap_trace_stop();
    ESP_LOGW(TAG, "==== outstanding allocations from the fetch pass ====");
    heap_trace_dump();
    ESP_LOGW(TAG, "==== end of leak dump ====");
#endif
}

void WifiStress_Run(void)
{
    const int cycles = CONFIG_CLOCK_WIFI_STRESS_CYCLES;

    ESP_LOGW(TAG, "starting - this holds boot for a while, and is only ever "
                  "enabled deliberately");

    /* Fetch pass first. It is the one under suspicion, and running it while
       the link is still healthy means its numbers survive even if a later
       pass loses USB. */
    s_fetch_count = cycles;
    struct tm utc;
    const size_t before_fetch = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    WifiSync_SyncTimeOnce(&utc, CONFIG_CLOCK_WIFI_SSID, CONFIG_CLOCK_WIFI_PASS,
                          15000, fetch_many_cb, NULL);
    vTaskDelay(pdMS_TO_TICKS(1000));
    const long net_fetch = (long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) -
                           (long)before_fetch;

    const long net_radio = run_pass("radio", cycles, false);

    ESP_LOGW(TAG, "RESULT: %d fetches in one session %+ld B (incl. one Wi-Fi "
                  "session), %d radio-only cycles %+ld B",
             cycles, net_fetch, cycles, net_radio);
    ESP_LOGW(TAG, "min-ever free internal since boot: %u B",
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

#else  /* !CONFIG_CLOCK_WIFI_STRESS */

void WifiStress_Run(void) {}

#endif
