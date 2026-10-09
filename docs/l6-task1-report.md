# L6 Task 1 — Scheduling-delay investigation

Board: `native_sim/native/64`, Zephyr v4.4.2 (built and run via `docker/zephyr.sh`).

## 1. Guarantee and detector

**Guarantee:** control work for every timer event must *start* within
10 ms (`CONTROL_DEADLINE_US`) of the event becoming ready in the timer ISR.

**Detector** (`app/src/main.c`):

- The ISR stamps each event with `k_cycle_get_32()` and a sequence number.
- `control` computes `latency_us = now - ready_cyc` right after `k_msgq_get()`.
- Every miss increments `deadline_misses`; the warning is printed with
  `LOG_WRN_RATELIMIT` (1 s interval), so counts stay exact while output is bounded.
- Every event logs `event_done seq=<n> latency_us=<us> queue=<depth>`.
- Every 20 events: `summary processed= misses= max_latency_us= dropped=`.
- Queue overflow in the ISR is counted in `dropped_events`.
- CTF named events: `event_ready` (ISR), `event_start`, `event_done` (control).

## 2. Baseline (CONTROL=7, MAINTENANCE=4)

```
[00:00:00.545,000] <inf> homework: event_done seq=0 latency_us=45000 queue=0
[00:00:01.045,000] <wrn> homework: Skipped 2 messages
[00:00:01.045,000] <wrn> homework: deadline_miss count=3 seq=2 latency_us=45000
...
[00:00:05.295,000] <inf> homework: summary processed=20 misses=20 max_latency_us=45000 dropped=0
```

Every event misses the deadline by the same 45 ms, and no event is dropped.

### Kernel shell snapshot

```
uart:~$ kernel thread list
 0x4411a0 maintenance  priority: 4  state: pending
 0x441290 control      priority: 7  state: pending
*0x441d10 shell_uart   priority: 14 state: queued
 0x4413a8 logging      priority: 14 state: pending
```

The snapshot shows the inverted priorities (maintenance 4 > control 7), but
both threads look idle. The shell (prio 14) can only run after both have
finished, so it can never catch control waiting in the ready queue. This is
the snapshot limitation from the lecture: it cannot show how long a thread
was ready.

### CTF trace (babeltrace2, seq=3)

```
01.250000000 msgq_put_enter
01.250000000 thread_sched_ready:  name = "control"       <- control is READY
01.250000000 named_event: event_ready arg0=3 arg1=0
01.250000000 thread_sched_ready:  name = "maintenance"
01.250000000 isr_exit
01.250000000 thread_switched_in:  name = "maintenance"   <- higher prio wins
01.250000000 thread_busy_wait_enter: usec_to_wait = 45000
01.295000000 thread_busy_wait_exit
01.295000000 thread_sched_pend:   name = "maintenance"
01.295000000 thread_switched_in:  name = "control"       <- 45 ms later
01.295000000 named_event: event_start arg0=3 arg1=45000
01.295000000 named_event: event_done  arg0=3 arg1=45000
```

`arg1=0` on `event_ready` is expected: control was already pending in
`k_msgq_get()`, so the message went straight to it and never sat in the queue.

## 3. Explanation

Using response time = waiting + scheduler delay + execution time:

- **Waiting (object delay): 0.** Control is made ready inside the same ISR
  that creates the event, and the queue never holds a backlog.
- **Scheduler delay: 45 ms.** Control is *ready* from 1.250 s, but
  `maintenance` (priority 4) is ready at the same time and outranks it. It
  runs a non-yielding 45 ms `k_busy_wait()` before control is chosen.
- **Execution: ~0.**

The cause is a priority assignment that does not match deadlines.
Maintenance has no short deadline but holds the higher priority.

## 4. Correction (one change)

`MAINTENANCE_PRIORITY` 4 → 10, so it now ranks below control (7). Priority now
follows deadline: shortest deadline, highest priority. Maintenance still sits
above shell/logging (14) and still does all of its work.

## 5. Re-measurement (same detector)

```
[00:00:05.250,000] <inf> homework: summary processed=20 misses=0 max_latency_us=0 dropped=0
```

```
01.250000000 thread_sched_ready:  name = "control"
01.250000000 named_event: event_ready arg0=3 arg1=0
01.250000000 thread_sched_ready:  name = "maintenance"
01.250000000 isr_exit
01.250000000 thread_switched_in:  name = "control"       <- immediately
01.250000000 named_event: event_start arg0=3 arg1=0
01.250000000 named_event: event_done  arg0=3 arg1=0
01.250000000 thread_switched_in:  name = "maintenance"
01.250000000 thread_busy_wait_enter: usec_to_wait = 45000
```

| Build                 | processed | misses | max latency |
|-----------------------|-----------|--------|-------------|
| Baseline (M=4, C=7)   | 20        | 20     | 45000 µs    |
| Corrected (M=10, C=7) | 20        | 0      | 0 µs        |

On native_sim, code between kernel calls takes zero simulated time, so the
0 µs is idealized. On real hardware, expect a few µs of ISR exit and context
switch time, still far under 10 ms. Confirm on the real target.

## Reproduce

```sh
docker/zephyr.sh west build -b native_sim/native/64 app -d build-sim -p always
mkdir -p build-sim/data
cp ../deps/zephyr/subsys/tracing/ctf/tsdl/metadata build-sim/data/
docker/zephyr.sh bash -c '(sleep 3; printf "kernel thread list\r\n"; sleep 4) | \
  ./build-sim/zephyr/zephyr.exe -uart_stdinout -stop_at=6 -trace-file=build-sim/data/channel0_0'
docker/zephyr.sh babeltrace2 build-sim/data/
```
