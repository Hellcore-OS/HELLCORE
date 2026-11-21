#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "sched.h"

static HELLCORE_SCHEDULER scheduler;
static HELLCORE_SCHED_THREAD mlfq_thread;
static HELLCORE_SCHED_THREAD cfs_thread;

static unsigned tests_run;

static void reset_scheduler(void)
{
    hellcore_sched_init(&scheduler, 30, 70, 1000);

    hellcore_sched_thread_init(
        &mlfq_thread, 1, HELLCORE_SCHED_CLASS_MLFQ, 1024, 0
    );

    hellcore_sched_thread_init(
        &cfs_thread, 2, HELLCORE_SCHED_CLASS_CFS, 1024, 0
    );
}

static void enqueue_both(void)
{
    assert(hellcore_sched_enqueue(&scheduler, &mlfq_thread, 0));
    assert(hellcore_sched_enqueue(&scheduler, &cfs_thread, 0));
}

static void test_init_and_invalid_shares(void)
{
    hellcore_sched_init(&scheduler, 30, 70, 1000);
    assert(scheduler.mlfq_share == 30);
    assert(scheduler.cfs_share == 70);
    assert(scheduler.tie_break_next_class ==
           HELLCORE_SCHED_CLASS_MLFQ);

    hellcore_sched_init(&scheduler, 0, 100, 1000);
    assert(scheduler.mlfq_share == HELLCORE_SCHED_DEFAULT_MLFQ_SHARE);
    assert(scheduler.cfs_share == HELLCORE_SCHED_DEFAULT_CFS_SHARE);

    tests_run++;
    puts("PASS: initialization and invalid-share fallback");
}

static void test_equal_service_tie_break(void)
{
    reset_scheduler();
    enqueue_both();

    HELLCORE_SCHED_THREAD *selected =
        hellcore_sched_pick_next(&scheduler);

    assert(selected == &mlfq_thread);
    assert(hellcore_sched_yield(&scheduler, 1));

    selected = hellcore_sched_pick_next(&scheduler);
    assert(selected == &cfs_thread);

    tests_run++;
    puts("PASS: equal-service class tie-break alternates");
}

static void test_30_70_service_distribution(void)
{
    reset_scheduler();
    enqueue_both();

    uint64_t now = 0;
    uint64_t mlfq_service = 0;
    uint64_t cfs_service = 0;

    for (unsigned i = 0; i < 1000; i++) {
        HELLCORE_SCHED_THREAD *selected =
            hellcore_sched_pick_next(&scheduler);

        assert(selected != NULL);
        assert(hellcore_sched_account(&scheduler, 1000));

        if (selected == &mlfq_thread)
            mlfq_service += 1000;
        else {
            assert(selected == &cfs_thread);
            cfs_service += 1000;
        }

        now += 1000;
        assert(hellcore_sched_yield(&scheduler, now));
    }

    assert(mlfq_service + cfs_service == 1000000);
    assert(mlfq_service >= 299000 && mlfq_service <= 301000);
    assert(cfs_service >= 699000 && cfs_service <= 701000);

    tests_run++;
    puts("PASS: competitive service approximates 30/70 share");
}

static void test_solo_class_borrowing(void)
{
    reset_scheduler();

    assert(hellcore_sched_enqueue(&scheduler, &mlfq_thread, 0));
    assert(hellcore_sched_pick_next(&scheduler) == &mlfq_thread);
    assert(hellcore_sched_account(&scheduler, 5000));

    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] == 0);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] == 0);

    assert(hellcore_sched_enqueue(&scheduler, &cfs_thread, 10));
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] == 0);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] == 0);

    tests_run++;
    puts("PASS: solo-class borrowing does not accrue share debt");
}

static void test_block_and_exit_transitions(void)
{
    reset_scheduler();
    enqueue_both();

    assert(hellcore_sched_pick_next(&scheduler) == &mlfq_thread);
    assert(hellcore_sched_block_current(&scheduler));
    assert(mlfq_thread.state == HELLCORE_THREAD_BLOCKED);
    assert(scheduler.current == NULL);

    assert(hellcore_sched_pick_next(&scheduler) == &cfs_thread);
    assert(hellcore_sched_exit_current(&scheduler));
    assert(cfs_thread.state == HELLCORE_THREAD_EXITED);
    assert(scheduler.current == NULL);

    tests_run++;
    puts("PASS: block and exit lifecycle transitions");
}

static void test_invalid_and_duplicate_enqueue(void)
{
    reset_scheduler();

    assert(hellcore_sched_enqueue(&scheduler, &mlfq_thread, 0));
    assert(!hellcore_sched_enqueue(&scheduler, &mlfq_thread, 0));

    HELLCORE_SCHED_THREAD invalid = {0};
    invalid.sched_class = (HELLCORE_SCHED_CLASS)99;
    invalid.state = HELLCORE_THREAD_NEW;

    assert(!hellcore_sched_enqueue(&scheduler, &invalid, 0));

    tests_run++;
    puts("PASS: duplicate and invalid enqueue rejected");
}

static void test_service_counter_rebase(void)
{
    reset_scheduler();
    enqueue_both();

    assert(hellcore_sched_pick_next(&scheduler) == &mlfq_thread);

    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] =
        UINT64_MAX - 5;
    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] =
        UINT64_MAX - 5;

    assert(hellcore_sched_account(&scheduler, 10));

    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] <
           UINT64_MAX);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] <
           UINT64_MAX);
    assert(mlfq_thread.mlfq_quantum_used_ns == 10);
    assert(mlfq_thread.total_runtime_ns == 10);

    tests_run++;
    puts("PASS: service counters rebase before overflow");
}

static void test_accounting_overflow_rollback(void)
{
    reset_scheduler();
    enqueue_both();

    assert(hellcore_sched_pick_next(&scheduler) == &mlfq_thread);

    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] =
        UINT64_MAX - 5;
    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] = 0;

    mlfq_thread.mlfq_quantum_used_ns = 7;
    mlfq_thread.total_runtime_ns = 11;

    assert(!hellcore_sched_account(&scheduler, 10));

    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] ==
           UINT64_MAX - 5);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] == 0);
    assert(mlfq_thread.mlfq_quantum_used_ns == 7);
    assert(mlfq_thread.total_runtime_ns == 11);

    tests_run++;
    puts("PASS: unrepresentable service update rolls back");
}


static void test_cfs_weighted_accounting_through_policy(void)
{
    reset_scheduler();

    cfs_thread.weight = 2048;
    enqueue_both();

    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] = 3000;

    assert(hellcore_sched_pick_next(&scheduler) == &cfs_thread);
    assert(hellcore_sched_account(&scheduler, 1000));

    assert(cfs_thread.vruntime == 500);
    assert(cfs_thread.total_runtime_ns == 1000);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] == 1000);

    tests_run++;
    puts("PASS: policy preserves weighted CFS accounting");
}

static void test_cfs_accounting_failure_rollback(void)
{
    reset_scheduler();
    enqueue_both();

    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] = 3000;
    assert(hellcore_sched_pick_next(&scheduler) == &cfs_thread);

    cfs_thread.vruntime = UINT64_MAX - 100;
    cfs_thread.total_runtime_ns = 17;

    uint64_t cfs_service =
        scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS];

    assert(!hellcore_sched_account(&scheduler, 1000));

    assert(cfs_thread.vruntime == UINT64_MAX - 100);
    assert(cfs_thread.total_runtime_ns == 17);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] ==
           cfs_service);
    assert(scheduler.current == &cfs_thread);
    assert(cfs_thread.state == HELLCORE_THREAD_RUNNING);

    tests_run++;
    puts("PASS: CFS accounting overflow rolls back policy state");
}

static void test_policy_mlfq_aging_interval(void)
{
    reset_scheduler();

    mlfq_thread.mlfq_priority = 2;
    assert(hellcore_sched_enqueue(&scheduler, &mlfq_thread, 0));

    hellcore_sched_age(&scheduler, 999);
    assert(mlfq_thread.mlfq_priority == 2);
    assert(scheduler.last_aging_ns == 0);

    hellcore_sched_age(&scheduler, 1000);
    assert(mlfq_thread.mlfq_priority == 1);
    assert(mlfq_thread.mlfq_wait_start_ns == 1000);
    assert(scheduler.last_aging_ns == 1000);

    hellcore_sched_age(&scheduler, 1500);
    assert(mlfq_thread.mlfq_priority == 1);
    assert(scheduler.last_aging_ns == 1000);

    hellcore_sched_age(&scheduler, 2000);
    assert(mlfq_thread.mlfq_priority == 0);
    assert(scheduler.last_aging_ns == 2000);

    hellcore_sched_age(&scheduler, 1999);
    assert(mlfq_thread.mlfq_priority == 0);
    assert(scheduler.last_aging_ns == 2000);

    tests_run++;
    puts("PASS: policy MLFQ aging interval and timestamp handling");
}

static void test_service_reset_when_class_disappears(void)
{
    reset_scheduler();
    enqueue_both();

    assert(hellcore_sched_pick_next(&scheduler) == &mlfq_thread);
    assert(hellcore_sched_account(&scheduler, 1000));
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] == 1000);

    assert(hellcore_sched_block_current(&scheduler));
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] == 0);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] == 0);

    assert(hellcore_sched_pick_next(&scheduler) == &cfs_thread);
    assert(hellcore_sched_account(&scheduler, 2000));
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] == 0);
    assert(scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] == 0);

    tests_run++;
    puts("PASS: service counters reset when a class disappears");
}

static void test_invalid_scheduler_configuration(void)
{
    reset_scheduler();
    enqueue_both();

    scheduler.mlfq_share = 0;
    assert(hellcore_sched_pick_next(&scheduler) == NULL);
    assert(scheduler.current == NULL);

    scheduler.mlfq_share = 30;
    scheduler.cfs_share = 70;
    scheduler.tie_break_next_class = (HELLCORE_SCHED_CLASS)99;

    assert(hellcore_sched_pick_next(&scheduler) == NULL);
    assert(scheduler.current == NULL);

    HELLCORE_SCHED_THREAD extra;
    hellcore_sched_thread_init(
        &extra, 3, HELLCORE_SCHED_CLASS_MLFQ, 1024, 0
    );

    assert(!hellcore_sched_enqueue(&scheduler, &extra, 0));

    tests_run++;
    puts("PASS: invalid scheduler configuration rejected safely");
}

static void test_policy_enqueue_capacity_rollback(void)
{
    reset_scheduler();

    scheduler.mlfq[0].count = UINT32_MAX;

    assert(!hellcore_sched_enqueue(&scheduler, &mlfq_thread, 0));
    assert(mlfq_thread.state == HELLCORE_THREAD_NEW);
    assert(mlfq_thread.next == NULL);
    assert(scheduler.current == NULL);
    assert(scheduler.runnable_count == 0);

    tests_run++;
    puts("PASS: policy enqueue capacity failure preserves thread");
}

static void test_mlfq_pick_failure_rollback(void)
{
    reset_scheduler();
    enqueue_both();

    HELLCORE_SCHED_QUEUE *queue = &scheduler.mlfq[0];
    HELLCORE_SCHED_THREAD *original_head = queue->head;
    HELLCORE_SCHED_THREAD *original_tail = queue->tail;

    mlfq_thread.state = HELLCORE_THREAD_BLOCKED;

    assert(scheduler.tie_break_next_class ==
           HELLCORE_SCHED_CLASS_MLFQ);
    assert(hellcore_sched_pick_next(&scheduler) == NULL);

    assert(scheduler.current == NULL);
    assert(scheduler.tie_break_next_class ==
           HELLCORE_SCHED_CLASS_MLFQ);
    assert(queue->head == original_head);
    assert(queue->tail == original_tail);
    assert(queue->count == 1);
    assert(scheduler.runnable_count == 2);
    assert(mlfq_thread.state == HELLCORE_THREAD_BLOCKED);
    assert(mlfq_thread.next == NULL);
    assert(scheduler.cfs_head == &cfs_thread);
    assert(scheduler.cfs_count == 1);

    tests_run++;
    puts("PASS: MLFQ pick failure preserves queues and policy tie-break");
}

static void test_cfs_pick_failure_rollback(void)
{
    reset_scheduler();
    enqueue_both();

    HELLCORE_SCHED_THREAD *original_head = scheduler.cfs_head;

    cfs_thread.state = HELLCORE_THREAD_BLOCKED;

    /* Make CFS the policy-selected class. */
    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] = 3000;
    scheduler.class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] = 0;

    assert(hellcore_sched_pick_next(&scheduler) == NULL);

    assert(scheduler.current == NULL);
    assert(scheduler.cfs_head == original_head);
    assert(scheduler.cfs_count == 1);
    assert(scheduler.runnable_count == 2);
    assert(cfs_thread.state == HELLCORE_THREAD_BLOCKED);
    assert(cfs_thread.next == NULL);
    assert(scheduler.mlfq[0].head == &mlfq_thread);
    assert(scheduler.mlfq[0].count == 1);

    tests_run++;
    puts("PASS: CFS pick failure preserves queues and scheduler state");
}

int main(void)
{
    test_init_and_invalid_shares();
    test_equal_service_tie_break();
    test_30_70_service_distribution();
    test_solo_class_borrowing();
    test_block_and_exit_transitions();
    test_invalid_and_duplicate_enqueue();
    test_service_counter_rebase();
    test_accounting_overflow_rollback();
    test_cfs_weighted_accounting_through_policy();
    test_cfs_accounting_failure_rollback();
    test_policy_mlfq_aging_interval();
    test_service_reset_when_class_disappears();
    test_invalid_scheduler_configuration();
    test_policy_enqueue_capacity_rollback();
    test_mlfq_pick_failure_rollback();
    test_cfs_pick_failure_rollback();

    printf("POLICY TESTS: %u passed, 0 failed\n", tests_run);
    return 0;
}
