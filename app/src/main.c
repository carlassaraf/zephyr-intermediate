/*
 * Lecture 4 Homework
 *
 * GOAL: Build a small event-driven system with zbus
 *
 * Create one zbus channel for sensor data
 * Publish simulated sensor samples every 100 ms
 * Add one listener for fast display updates
 * Add one subscriber for slower logging
 *
 * ================================================================
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/random/random.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE      1024
#define SENSOR_MS       100
#define WDT_TIMEOUT_MS  (SENSOR_MS * 5)
#define MSG_DEPTH       10
#define HEALTH_CHECK_MS 200

struct sensor_data {
    uint32_t timestamp_ms;  /**< Timestamp for the pub data */
    int32_t temperature_mc; /**< Temperature in mili Celsius */
    uint8_t seq;            /**< Number of published data */
};

/** Forward declarations */

K_MSGQ_DEFINE(msgq, sizeof(struct sensor_data), MSG_DEPTH, 4);

/** Sensor simulation called every */
static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
    static uint8_t seq = 0;
    while (1) {
        uint32_t random_value = sys_rand32_get();
        struct sensor_data data = {
            .seq = seq++,
            .timestamp_ms = k_uptime_get_32(),
            .temperature_mc = 25000 + (random_value % 1000)
        };
        LOG_INF("[SENSOR] seq %d tick=%u temp=%u mC", data.seq, data.timestamp_ms, data.temperature_mc);
        k_msgq_put(&msgq, &data, K_MSEC(SENSOR_MS));
        k_msleep(SENSOR_MS);
    }
}

/* Called when watchdog is triggered avoiding sys_reboot() */
static void consumer_stuck_cb(int channel_id, void *user_data)
{
    k_tid_t tid = (k_tid_t)user_data;
    LOG_ERR("[WATCHDOG] channel %d timed out, thread '%s' appears stuck",
        channel_id, k_thread_name_get(tid));
    task_wdt_feed(channel_id);
}

/** Sensor consumer thread */
static void sensor_consumer_fn(void *p1, void *p2, void *p3)
{
    static int stuck_count = 0;
    int wdt_id = task_wdt_add(WDT_TIMEOUT_MS, consumer_stuck_cb, (void *)k_current_get());
    while (1) {
        struct sensor_data data = {0};
        if ((stuck_count++ % 20) == 19) {
            LOG_WRN("[CONSUMER] simulating a stuck thread...");
            k_sleep(K_SECONDS(5));
        }
        if (k_msgq_get(&msgq, &data, K_MSEC(SENSOR_MS)) == 0) {
            /* got an item successfully */
            task_wdt_feed(wdt_id);
        }
        k_msleep(SENSOR_MS);
    }
}

/* Health-check thread */
static void health_check_thread_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        uint32_t used = k_msgq_num_used_get(&msgq);
        uint32_t cap  = msgq.max_msgs;
        if (used * 100 / cap >= 75) {
            LOG_WRN("[HEALTH] queue at %u%% capacity (%u/%u)", used * 100 / cap, used, cap);
        }
        k_msleep(HEALTH_CHECK_MS);
    }
}

/* Producer thread */
K_THREAD_DEFINE(producer_thread, STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(consumer_thread, STACK_SIZE, sensor_consumer_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(health_check_thread, STACK_SIZE, health_check_thread_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    const struct device *hw_wdt = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
    task_wdt_init(device_is_ready(hw_wdt) ? hw_wdt : NULL);

    LOG_INF("=== L3 Homework: Polling to Workqueue ===");
    LOG_INF("Run this, count wakeups, then convert to workqueue.");
    return 0;
}