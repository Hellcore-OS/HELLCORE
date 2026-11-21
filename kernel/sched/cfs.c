#include "cfs.h"

static void cfs_update_min_vruntime(
    HELLCORE_SCHEDULER *scheduler
) {
    uint64_t candidate = UINT64_MAX;

    if (scheduler->current &&
        scheduler->current->sched_class == HELLCORE_SCHED_CLASS_CFS &&
        scheduler->current->vruntime < candidate) {
        candidate = scheduler->current->vruntime;
    }

    if (scheduler->cfs_head &&
        scheduler->cfs_head->vruntime < candidate) {
        candidate = scheduler->cfs_head->vruntime;
    }

    if (candidate != UINT64_MAX &&
        candidate > scheduler->cfs_min_vruntime) {
        scheduler->cfs_min_vruntime = candidate;
    }
}

static void cfs_insert_sorted(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread
) {
    HELLCORE_SCHED_THREAD **link = &scheduler->cfs_head;

    /*
     * Insert after equal vruntime entries to preserve FIFO
     * ordering among threads with equal virtual runtime.
     */
    while (*link && (*link)->vruntime <= thread->vruntime)
        link = &(*link)->next;

    thread->next = *link;
    *link = thread;
    scheduler->cfs_count++;
    scheduler->runnable_count++;
}

bool hellcore_cfs_enqueue(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread
) {
    if (!scheduler || !thread ||
        thread->sched_class != HELLCORE_SCHED_CLASS_CFS ||
        (thread->state != HELLCORE_THREAD_NEW &&
         thread->state != HELLCORE_THREAD_BLOCKED) ||
        thread->next != NULL ||
        thread == scheduler->current ||
        thread->weight == 0 ||
        scheduler->cfs_count == UINT32_MAX ||
        scheduler->runnable_count == UINT32_MAX) {
        return false;
    }

    /*
     * Clamp new and awakened tasks to the current CFS floor.
     * This prevents a sleeper from accumulating an unfair
     * virtual-runtime advantage while it is not runnable.
     */
    if (thread->vruntime < scheduler->cfs_min_vruntime)
        thread->vruntime = scheduler->cfs_min_vruntime;

    thread->state = HELLCORE_THREAD_RUNNABLE;
    cfs_insert_sorted(scheduler, thread);

    return true;
}

HELLCORE_SCHED_THREAD *hellcore_cfs_pick(
    HELLCORE_SCHEDULER *scheduler
) {
    if (!scheduler || scheduler->current ||
        !scheduler->cfs_head ||
        scheduler->cfs_count == 0 ||
        scheduler->runnable_count == 0) {
        return NULL;
    }

    HELLCORE_SCHED_THREAD *thread = scheduler->cfs_head;

    if (thread->sched_class != HELLCORE_SCHED_CLASS_CFS ||
        thread->state != HELLCORE_THREAD_RUNNABLE ||
        (scheduler->cfs_count == 1 && thread->next != NULL) ||
        (scheduler->cfs_count > 1 && thread->next == NULL)) {
        return NULL;
    }

    scheduler->cfs_head = thread->next;
    scheduler->cfs_count--;
    scheduler->runnable_count--;

    thread->next = NULL;
    thread->state = HELLCORE_THREAD_RUNNING;
    scheduler->current = thread;

    cfs_update_min_vruntime(scheduler);

    return thread;
}

bool hellcore_cfs_account_current(
    HELLCORE_SCHEDULER *scheduler,
    uint64_t elapsed_ns
) {
    if (!scheduler || !scheduler->current)
        return false;

    HELLCORE_SCHED_THREAD *thread = scheduler->current;

    if (thread->sched_class != HELLCORE_SCHED_CLASS_CFS ||
        thread->state != HELLCORE_THREAD_RUNNING ||
        thread->weight == 0) {
        return false;
    }

    /*
     * Compute floor(elapsed_ns * base_weight / weight)
     * without overflowing the intermediate multiplication.
     */
    uint64_t quotient = elapsed_ns / thread->weight;
    uint64_t remainder = elapsed_ns % thread->weight;

    if (quotient > UINT64_MAX / HELLCORE_CFS_BASE_WEIGHT)
        return false;

    uint64_t scaled =
        quotient * HELLCORE_CFS_BASE_WEIGHT;

    uint64_t fractional =
        (remainder * HELLCORE_CFS_BASE_WEIGHT) / thread->weight;

    if (scaled > UINT64_MAX - fractional)
        return false;

    uint64_t delta_vruntime = scaled + fractional;

    if (thread->vruntime > UINT64_MAX - delta_vruntime ||
        thread->total_runtime_ns > UINT64_MAX - elapsed_ns) {
        return false;
    }

    thread->vruntime += delta_vruntime;
    thread->total_runtime_ns += elapsed_ns;

    cfs_update_min_vruntime(scheduler);

    return true;
}

bool hellcore_cfs_requeue_current(
    HELLCORE_SCHEDULER *scheduler
) {
    if (!scheduler || !scheduler->current)
        return false;

    HELLCORE_SCHED_THREAD *thread = scheduler->current;

    if (thread->sched_class != HELLCORE_SCHED_CLASS_CFS ||
        thread->state != HELLCORE_THREAD_RUNNING ||
        thread->next != NULL ||
        scheduler->cfs_count == UINT32_MAX ||
        scheduler->runnable_count == UINT32_MAX) {
        return false;
    }

    thread->state = HELLCORE_THREAD_RUNNABLE;
    cfs_insert_sorted(scheduler, thread);
    scheduler->current = NULL;
    cfs_update_min_vruntime(scheduler);

    return true;
}
