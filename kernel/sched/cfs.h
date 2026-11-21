#ifndef HELLCORE_CFS_H
#define HELLCORE_CFS_H

#include "sched.h"

bool hellcore_cfs_enqueue(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread
);

HELLCORE_SCHED_THREAD *hellcore_cfs_pick(
    HELLCORE_SCHEDULER *scheduler
);

bool hellcore_cfs_account_current(
    HELLCORE_SCHEDULER *scheduler,
    uint64_t elapsed_ns
);

bool hellcore_cfs_requeue_current(
    HELLCORE_SCHEDULER *scheduler
);

#endif
