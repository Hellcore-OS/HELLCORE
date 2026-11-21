#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/sched/mlfq.h"

static HELLCORE_SCHEDULER scheduler;
static HELLCORE_SCHED_THREAD threads[8];

static void reset_scheduler(void)
{
    memset(&scheduler, 0, sizeof(scheduler));
    memset(threads, 0, sizeof(threads));

    scheduler.aging_interval_ns = 100;

    for (uint64_t i = 0; i < 8; i++) {
        hellcore_sched_thread_init(
            &threads[i],
            i + 1,
            HELLCORE_SCHED_CLASS_MLFQ,
            HELLCORE_CFS_BASE_WEIGHT,
            0
        );
    }
}

static void test_priority_and_fifo(void)
{
    reset_scheduler();

    threads[0].mlfq_priority = 2;
    threads[1].mlfq_priority = 0;
    threads[2].mlfq_priority = 0;

    assert(hellcore_mlfq_enqueue(&scheduler, &threads[0], 0));
    assert(hellcore_mlfq_enqueue(&scheduler, &threads[1], 0));
    assert(hellcore_mlfq_enqueue(&scheduler, &threads[2], 0));

    assert(hellcore_mlfq_pick(&scheduler) == &threads[1]);
    assert(scheduler.current == &threads[1]);
    scheduler.current->state = HELLCORE_THREAD_BLOCKED;
    scheduler.current = NULL;

    assert(hellcore_mlfq_pick(&scheduler) == &threads[2]);
    scheduler.current->state = HELLCORE_THREAD_BLOCKED;
    scheduler.current = NULL;

    assert(hellcore_mlfq_pick(&scheduler) == &threads[0]);

    puts("PASS: priority ordering and FIFO");
}

static void test_quantum_demotion(void)
{
    reset_scheduler();

    threads[0].mlfq_priority = 1;
    threads[0].mlfq_quantum_used_ns = 4000000;

    assert(hellcore_mlfq_enqueue(&scheduler, &threads[0], 10));
    assert(hellcore_mlfq_pick(&scheduler) == &threads[0]);

    assert(hellcore_mlfq_requeue_expired(
        &scheduler, &threads[0], 20
    ));

    assert(threads[0].mlfq_priority == 2);
    assert(threads[0].mlfq_quantum_used_ns == 0);
    assert(threads[0].state == HELLCORE_THREAD_RUNNABLE);
    assert(scheduler.current == NULL);

    puts("PASS: quantum expiration demotion");
}

static void test_aging_promotion(void)
{
    reset_scheduler();

    threads[0].mlfq_priority = 3;
    threads[1].mlfq_priority = 2;

    assert(hellcore_mlfq_enqueue(&scheduler, &threads[0], 0));
    assert(hellcore_mlfq_enqueue(&scheduler, &threads[1], 50));

    hellcore_mlfq_age(&scheduler, 100);

    assert(threads[0].mlfq_priority == 2);
    assert(threads[1].mlfq_priority == 2);
    assert(scheduler.runnable_count == 2);
    assert(scheduler.mlfq[2].count == 2);

    puts("PASS: aging promotion and runnable count");
}

static void test_invalid_enqueue(void)
{
    reset_scheduler();

    threads[0].sched_class = HELLCORE_SCHED_CLASS_CFS;

    assert(!hellcore_mlfq_enqueue(&scheduler, &threads[0], 0));
    assert(scheduler.runnable_count == 0);

    puts("PASS: invalid class rejected");
}


static void test_duplicate_enqueue_rejected(void)
{
    reset_scheduler();

    assert(hellcore_mlfq_enqueue(&scheduler, &threads[0], 10));
    assert(!hellcore_mlfq_enqueue(&scheduler, &threads[0], 20));

    assert(scheduler.runnable_count == 1);
    assert(scheduler.mlfq[0].count == 1);
    assert(scheduler.mlfq[0].head == &threads[0]);
    assert(scheduler.mlfq[0].tail == &threads[0]);

    puts("PASS: duplicate enqueue rejected without queue damage");
}

static void test_enqueue_overflow_rollback(void)
{
    reset_scheduler();

    threads[0].state = HELLCORE_THREAD_BLOCKED;
    threads[0].mlfq_wait_start_ns = 77;
    scheduler.runnable_count = UINT32_MAX;

    assert(!hellcore_mlfq_enqueue(&scheduler, &threads[0], 100));
    assert(threads[0].state == HELLCORE_THREAD_BLOCKED);
    assert(threads[0].mlfq_wait_start_ns == 77);
    assert(threads[0].next == NULL);

    puts("PASS: enqueue overflow rollback");
}

static void test_expiration_overflow_rollback(void)
{
    reset_scheduler();

    threads[0].mlfq_priority = 1;
    threads[0].mlfq_quantum_used_ns = 1234;
    threads[0].mlfq_wait_start_ns = 55;

    assert(hellcore_mlfq_enqueue(&scheduler, &threads[0], 10));
    assert(hellcore_mlfq_pick(&scheduler) == &threads[0]);

    scheduler.runnable_count = UINT32_MAX;

    assert(!hellcore_mlfq_requeue_expired(
        &scheduler, &threads[0], 200
    ));

    assert(scheduler.current == &threads[0]);
    assert(threads[0].state == HELLCORE_THREAD_RUNNING);
    assert(threads[0].mlfq_priority == 1);
    assert(threads[0].mlfq_quantum_used_ns == 1234);
    assert(threads[0].mlfq_wait_start_ns == 10);

    puts("PASS: expiration overflow rollback");
}

static void test_aging_full_destination(void)
{
    reset_scheduler();

    threads[0].mlfq_priority = 1;
    assert(hellcore_mlfq_enqueue(&scheduler, &threads[0], 0));

    scheduler.mlfq[0].count = UINT32_MAX;

    hellcore_mlfq_age(&scheduler, 100);

    assert(threads[0].mlfq_priority == 1);
    assert(scheduler.mlfq[1].head == &threads[0]);
    assert(scheduler.mlfq[1].count == 1);
    assert(scheduler.runnable_count == 1);

    puts("PASS: aging skips full destination queue");
}

static void test_aging_fifo_integrity(void)
{
    reset_scheduler();

    threads[0].mlfq_priority = 2;
    threads[1].mlfq_priority = 2;
    threads[2].mlfq_priority = 2;

    assert(hellcore_mlfq_enqueue(&scheduler, &threads[0], 0));
    assert(hellcore_mlfq_enqueue(&scheduler, &threads[1], 0));
    assert(hellcore_mlfq_enqueue(&scheduler, &threads[2], 0));

    hellcore_mlfq_age(&scheduler, 100);

    assert(scheduler.mlfq[1].count == 3);
    assert(scheduler.mlfq[1].head == &threads[0]);
    assert(threads[0].next == &threads[1]);
    assert(threads[1].next == &threads[2]);
    assert(scheduler.mlfq[1].tail == &threads[2]);
    assert(scheduler.runnable_count == 3);

    puts("PASS: aging preserves FIFO order");
}

int main(void)
{
    test_priority_and_fifo();
    test_quantum_demotion();
    test_aging_promotion();
    test_invalid_enqueue();
    test_duplicate_enqueue_rejected();
    test_enqueue_overflow_rollback();
    test_expiration_overflow_rollback();
    test_aging_full_destination();
    test_aging_fifo_integrity();

    puts("MLFQ TESTS: 9 passed, 0 failed");
    return 0;
}
