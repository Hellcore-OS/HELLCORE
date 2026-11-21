#ifndef HELLCORE_MLFQ_H
#define HELLCORE_MLFQ_H

#include "sched.h"

bool hellcore_mlfq_enqueue(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread,
    uint64_t now_ns
);

HELLCORE_SCHED_THREAD *hellcore_mlfq_pick(
    HELLCORE_SCHEDULER *scheduler
);

bool hellcore_mlfq_requeue_expired(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread,
    uint64_t now_ns
);

bool hellcore_mlfq_requeue_yield(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread,
    uint64_t now_ns
);

void hellcore_mlfq_age(
    HELLCORE_SCHEDULER *scheduler,
    uint64_t now_ns
);

#endif
