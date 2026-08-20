#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define STACK_SIZE  1024
#define PRIO        2
#define MAX_COUNT   100000

static volatile uint32_t counter = 0;

K_MUTEX_DEFINE(mutex);
K_SEM_DEFINE(sem, 0, 2);

void worker(void *p1, void *p2, void *p3)
{
    const char *name = k_thread_name_get(k_current_get());
    for (uint32_t i = 0; i < MAX_COUNT; i++) {
        k_mutex_lock(&mutex, K_FOREVER);
        counter++;
        k_mutex_unlock(&mutex);
    }
    LOG_INF("[%s] Done", name);
    k_sem_give(&sem);
}

K_THREAD_DEFINE(worker_a, STACK_SIZE, worker,
                NULL, NULL, NULL, PRIO, 0, 0);
K_THREAD_DEFINE(worker_b, STACK_SIZE, worker,
                NULL, NULL, NULL, PRIO, 0, 0);

int main(void)
{
    k_sem_take(&sem, K_FOREVER);
    k_sem_take(&sem, K_FOREVER);
    LOG_INF("Expected: counter = %d", 2 * MAX_COUNT);
    LOG_INF("Result: counter = %d", counter);

    if (counter == 2 * MAX_COUNT) {
        LOG_WRN("No race condition met");
    } else {
        LOG_ERR("Race condition was met");
    }
    return 0;
}

