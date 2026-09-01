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

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE    1024
#define SENSOR_MS     100

struct sensor_data {
    uint32_t timestamp_ms;  /**< Timestamp for the pub data */
    int32_t temperature_mc; /**< Temperature in mili Celsius */
    uint8_t seq;            /**< Number of published data */
};

/** Forward declarations */

static void display_listener_cb(const struct zbus_channel *chan);

/** Listener */
ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);
/** Subscriber */
ZBUS_MSG_SUBSCRIBER_DEFINE(logger_sub);
/** Channel */
ZBUS_CHAN_DEFINE(sensor_chan,   /**< Name */
    struct sensor_data,         /**< Channel data */
    NULL, NULL,                 /**< Validator, User data */
    ZBUS_OBSERVERS(display_lis, logger_sub),  /**< Observers */
    ZBUS_MSG_INIT(              /**< Initial value */
        .temperature_mc = 0,
        .timestamp_ms = 0,
        .seq = 0
    )
);

/** Synchronous observer called from the producer context */
static void display_listener_cb(const struct zbus_channel *chan)
{
    const struct sensor_data *msg = (const struct sensor_data*)zbus_chan_const_msg(chan);
    /** Non blocking work */
    LOG_INF("[LISTENER] Message %d received: %d mC @ %d in thread %s", msg->seq, msg->temperature_mc, msg->timestamp_ms, k_thread_name_get(k_current_get()));
}

/** Asynchronous observer called from a separate thread */
static void logger_fn(void *p1, void *p2, void *p3)
{
    const struct zbus_channel *chan;
    while (1) {
        struct sensor_data msg;
        int ret = zbus_sub_wait_msg(&logger_sub, &chan, &msg, K_MSEC(1000));
        if (ret) {
            LOG_ERR("[LOGGER] timeout");
        } else {
            LOG_INF("[LOGGER] Message %d received: %d mC @ %d in thread %s", msg.seq, msg.temperature_mc, msg.timestamp_ms, k_thread_name_get(k_current_get()));
        }
    }
}

/** Sensor simulation called every 199 ns */
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
        zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));
        k_msleep(SENSOR_MS);
    }
}

/* Producer thread */
K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    LOG_INF("=== L3 Homework: Polling to Workqueue ===");
    LOG_INF("Run this, count wakeups, then convert to workqueue.");
    return 0;
}