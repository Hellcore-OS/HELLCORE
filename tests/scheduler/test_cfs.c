#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/sched/cfs.h"

static HELLCORE_SCHEDULER scheduler;
static HELLCORE_SCHED_THREAD threads[8];

static void reset_scheduler(void)
{
    memset(&scheduler, 0, sizeof(scheduler));
    memset(threads, 0, sizeof(threads));

    hellcore_sched_init(&scheduler, 30, 70, 100);

    for (uint64_t i = 0; i < 8; i++) {
        hellcore_sched_thread_init(
            &threads[i],
            i + 1,
            HELLCORE_SCHED_CLASS_CFS,
            HELLCORE_CFS_BASE_WEIGHT,
            0
        );
    }
}

static void test_lowest_vruntime_selected(void)
{
    reset_scheduler();

    threads[0].vruntime = 300;
    threads[1].vruntime = 100;
    threads[2].vruntime = 200;

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_enqueue(&scheduler, &threads[1]));
    assert(hellcore_cfs_enqueue(&scheduler, &threads[2]));

    assert(scheduler.cfs_count == 3);
    assert(scheduler.runnable_count == 3);

    assert(hellcore_cfs_pick(&scheduler) == &threads[1]);
    assert(scheduler.current == &threads[1]);
    assert(scheduler.runnable_count == 2);

    puts("PASS: lowest vruntime selected");
}

static void test_equal_vruntime_fifo(void)
{
    reset_scheduler();

    threads[0].vruntime = 500;
    threads[1].vruntime = 500;
    threads[2].vruntime = 500;

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_enqueue(&scheduler, &threads[1]));
    assert(hellcore_cfs_enqueue(&scheduler, &threads[2]));

    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);
    assert(hellcore_cfs_requeue_current(&scheduler));

    assert(hellcore_cfs_pick(&scheduler) == &threads[1]);

    puts("PASS: equal vruntime FIFO ordering");
}

static void test_weighted_accounting(void)
{
    reset_scheduler();

    hellcore_sched_thread_init(
        &threads[0], 1, HELLCORE_SCHED_CLASS_CFS, 2048, 0
    );

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);

    assert(hellcore_cfs_account_current(&scheduler, 1000000));

    assert(threads[0].total_runtime_ns == 1000000);
    assert(threads[0].vruntime == 500000);

    puts("PASS: weighted virtual-runtime accounting");
}

static void test_wakeup_floor(void)
{
    reset_scheduler();

    scheduler.cfs_min_vruntime = 900;

    threads[0].vruntime = 100;

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));

    assert(threads[0].vruntime == 900);
    assert(scheduler.cfs_head == &threads[0]);

    puts("PASS: wake-up placement respects minimum vruntime");
}

static void test_requeue_sorted_after_accounting(void)
{
    reset_scheduler();

    threads[0].vruntime = 100;
    threads[1].vruntime = 200;

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_enqueue(&scheduler, &threads[1]));

    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);
    assert(hellcore_cfs_account_current(&scheduler, 200));
    assert(hellcore_cfs_requeue_current(&scheduler));

    assert(scheduler.cfs_head == &threads[1]);
    assert(scheduler.cfs_head->next == &threads[0]);
    assert(scheduler.cfs_count == 2);
    assert(scheduler.runnable_count == 2);
    assert(scheduler.current == NULL);

    puts("PASS: requeue restores vruntime ordering");
}

static void test_invalid_and_duplicate_enqueue(void)
{
    reset_scheduler();

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(!hellcore_cfs_enqueue(&scheduler, &threads[0]));

    assert(scheduler.cfs_count == 1);
    assert(scheduler.runnable_count == 1);

    hellcore_sched_thread_init(
        &threads[1], 2, HELLCORE_SCHED_CLASS_MLFQ,
        HELLCORE_CFS_BASE_WEIGHT, 0
    );

    assert(!hellcore_cfs_enqueue(&scheduler, &threads[1]));
    assert(scheduler.cfs_count == 1);
    assert(scheduler.runnable_count == 1);

    puts("PASS: invalid and duplicate enqueue rejected");
}

static void test_accounting_overflow_rollback(void)
{
    reset_scheduler();

    threads[0].vruntime = UINT64_MAX - 2;

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);

    uint64_t old_vruntime = threads[0].vruntime;
    uint64_t old_runtime = threads[0].total_runtime_ns;

    assert(!hellcore_cfs_account_current(&scheduler, 10));

    assert(threads[0].vruntime == old_vruntime);
    assert(threads[0].total_runtime_ns == old_runtime);
    assert(scheduler.current == &threads[0]);

    puts("PASS: accounting overflow rejected without mutation");
}


static void test_enqueue_capacity_rollback(void)
{
    reset_scheduler();

    scheduler.cfs_count = UINT32_MAX;

    uint64_t old_vruntime = threads[0].vruntime;

    assert(!hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(threads[0].state == HELLCORE_THREAD_NEW);
    assert(threads[0].vruntime == old_vruntime);
    assert(threads[0].next == NULL);
    assert(scheduler.cfs_count == UINT32_MAX);
    assert(scheduler.runnable_count == 0);

    puts("PASS: enqueue capacity rejection preserves thread");
}

static void test_zero_elapsed_accounting(void)
{
    reset_scheduler();

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);

    uint64_t old_vruntime = threads[0].vruntime;
    uint64_t old_runtime = threads[0].total_runtime_ns;

    assert(hellcore_cfs_account_current(&scheduler, 0));
    assert(threads[0].vruntime == old_vruntime);
    assert(threads[0].total_runtime_ns == old_runtime);
    assert(scheduler.current == &threads[0]);

    puts("PASS: zero elapsed accounting is a no-op");
}

static void test_requeue_capacity_rollback(void)
{
    reset_scheduler();

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);

    scheduler.cfs_count = UINT32_MAX;

    assert(!hellcore_cfs_requeue_current(&scheduler));
    assert(scheduler.current == &threads[0]);
    assert(threads[0].state == HELLCORE_THREAD_RUNNING);
    assert(threads[0].next == NULL);
    assert(scheduler.cfs_count == UINT32_MAX);

    puts("PASS: requeue capacity rejection preserves current thread");
}

static void test_vruntime_overflow_boundary(void)
{
    reset_scheduler();

    threads[0].vruntime = UINT64_MAX;

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);

    assert(hellcore_cfs_account_current(&scheduler, 0));
    assert(!hellcore_cfs_account_current(&scheduler, 1));

    assert(threads[0].vruntime == UINT64_MAX);
    assert(threads[0].total_runtime_ns == 0);
    assert(scheduler.current == &threads[0]);

    puts("PASS: maximum vruntime boundary handled safely");
}

static void test_min_vruntime_monotonicity(void)
{
    reset_scheduler();

    threads[0].vruntime = 100;
    threads[1].vruntime = 200;

    assert(hellcore_cfs_enqueue(&scheduler, &threads[0]));
    assert(hellcore_cfs_enqueue(&scheduler, &threads[1]));

    assert(hellcore_cfs_pick(&scheduler) == &threads[0]);
    assert(scheduler.cfs_min_vruntime == 100);

    assert(hellcore_cfs_account_current(&scheduler, 500));
    assert(scheduler.cfs_min_vruntime == 200);

    assert(hellcore_cfs_requeue_current(&scheduler));
    assert(scheduler.cfs_min_vruntime == 200);

    assert(scheduler.cfs_head == &threads[1]);

    puts("PASS: minimum vruntime remains monotonic");
}

int main(void)
{
    test_lowest_vruntime_selected();
    test_equal_vruntime_fifo();
    test_weighted_accounting();
    test_wakeup_floor();
    test_requeue_sorted_after_accounting();
    test_invalid_and_duplicate_enqueue();
    test_accounting_overflow_rollback();
    test_enqueue_capacity_rollback();
    test_zero_elapsed_accounting();
    test_requeue_capacity_rollback();
    test_vruntime_overflow_boundary();
    test_min_vruntime_monotonicity();

    puts("CFS TESTS: 12 passed, 0 failed");
    return 0;
}
