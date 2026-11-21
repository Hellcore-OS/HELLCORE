#include "sched.h"

void hellcore_sched_init(
    HELLCORE_SCHEDULER *scheduler,
    uint32_t mlfq_share,
    uint32_t cfs_share,
    uint64_t aging_interval_ns
) {
    if (!scheduler)
        return;

    *scheduler = (HELLCORE_SCHEDULER){0};

    if (mlfq_share == 0 || cfs_share == 0 ||
        mlfq_share > 100 || cfs_share > 100 ||
        mlfq_share + cfs_share != 100) {
        mlfq_share = HELLCORE_SCHED_DEFAULT_MLFQ_SHARE;
        cfs_share = HELLCORE_SCHED_DEFAULT_CFS_SHARE;
    }

    scheduler->mlfq_share = mlfq_share;
    scheduler->cfs_share = cfs_share;
    scheduler->tie_break_next_class = HELLCORE_SCHED_CLASS_MLFQ;
    scheduler->aging_interval_ns = aging_interval_ns;
}

void hellcore_sched_thread_init(
    HELLCORE_SCHED_THREAD *thread,
    uint64_t id,
    HELLCORE_SCHED_CLASS sched_class,
    uint32_t weight,
    uint64_t now_ns
) {
    if (!thread)
        return;

    *thread = (HELLCORE_SCHED_THREAD){0};

    thread->id = id;
    thread->sched_class = sched_class;
    thread->state = HELLCORE_THREAD_NEW;
    thread->mlfq_priority = 0;
    thread->mlfq_wait_start_ns = now_ns;

    if (weight == 0)
        weight = HELLCORE_CFS_BASE_WEIGHT;

    thread->weight = weight;
}
