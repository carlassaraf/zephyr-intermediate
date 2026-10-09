#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

#define STACK_SIZE            2048
#define CONTROL_PRIORITY         7
#define MAINTENANCE_PRIORITY    10
#define EVENT_PERIOD_MS        250
#define MAINTENANCE_LOAD_US  45000

/* Response-time guarantee: control work starts within 10 ms of the event. */
#define CONTROL_DEADLINE_US  10000
#define SUMMARY_EVERY           20

struct control_event {
    uint32_t seq;
    uint32_t ready_cyc;
};

K_MSGQ_DEFINE(control_queue, sizeof(struct control_event), 4, 4);
K_SEM_DEFINE(maintenance_start, 0, 1);

static atomic_t dropped_events;

/* ================================================================== */
/*  Timer expiry: creates one control event                           */
/* ================================================================== */

static void event_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    static uint32_t seq;
    struct control_event event = {
        .seq = seq++,
        .ready_cyc = k_cycle_get_32(),
    };

    /* Timer expiry runs in interrupt context, so never wait here. */
    int ret = k_msgq_put(&control_queue, &event, K_NO_WAIT);

    if (ret != 0) {
        atomic_inc(&dropped_events);
        return;
    }

    sys_trace_named_event("event_ready", event.seq,
                          k_msgq_num_used_get(&control_queue));

    /* Both threads become ready when the timer interrupt returns. */
    k_sem_give(&maintenance_start);
}

K_TIMER_DEFINE(event_timer, event_timer_expiry, NULL);

/* ================================================================== */
/*  Control thread                                                   */
/* ================================================================== */

static void control_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint32_t processed = 0;
    uint32_t deadline_misses = 0;
    uint32_t max_latency_us = 0;

    while (true) {
        struct control_event event;
        int ret = k_msgq_get(&control_queue, &event, K_FOREVER);

        if (ret != 0) {
            LOG_ERR("[CONTROL] receive failed: %d", ret);
            continue;
        }

        /* Start latency: event ready in the ISR -> control thread running. */
        uint32_t latency_us = k_cyc_to_us_floor32(k_cycle_get_32() - event.ready_cyc);

        sys_trace_named_event("event_start", event.seq, latency_us);

        processed++;
        max_latency_us = MAX(max_latency_us, latency_us);

        if (latency_us > CONTROL_DEADLINE_US) {
            deadline_misses++;
            LOG_WRN_RATELIMIT("deadline_miss count=%u seq=%u latency_us=%u",
                              deadline_misses, event.seq, latency_us);
        }

        LOG_INF("event_done seq=%u latency_us=%u queue=%u",
                event.seq, latency_us, k_msgq_num_used_get(&control_queue));

        sys_trace_named_event("event_done", event.seq, latency_us);

        if ((processed % SUMMARY_EVERY) == 0) {
            LOG_INF("summary processed=%u misses=%u max_latency_us=%u dropped=%u",
                    processed, deadline_misses, max_latency_us,
                    (uint32_t)atomic_get(&dropped_events));
        }
    }
}

/* ================================================================== */
/*  Background maintenance thread                                    */
/* ================================================================== */

static void maintenance_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (true) {
        k_sem_take(&maintenance_start, K_FOREVER);

        /* This work is important, but it has no short deadline. */
        k_busy_wait(MAINTENANCE_LOAD_US);
    }
}

K_THREAD_DEFINE(control, STACK_SIZE, control_fn,
                NULL, NULL, NULL, CONTROL_PRIORITY, 0, 0);

K_THREAD_DEFINE(maintenance, STACK_SIZE, maintenance_fn,
                NULL, NULL, NULL, MAINTENANCE_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("=== L6 Homework: Runtime Investigation ===");
    LOG_INF("Control work must start within 10 ms");
    LOG_INF("Inspect, measure, trace, explain, and correct the delay");

    k_timer_start(&event_timer, K_MSEC(500), K_MSEC(EVENT_PERIOD_MS));

    return 0;
}
