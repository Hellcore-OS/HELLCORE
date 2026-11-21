#include "mlfq.h"

static bool mlfq_append(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread
) {
    HELLCORE_SCHED_QUEUE *queue;

    if (!scheduler || !thread ||
        thread->mlfq_priority >= HELLCORE_MLFQ_LEVELS ||
        scheduler->runnable_count == UINT32_MAX) {
        return false;
    }

    queue = &scheduler->mlfq[thread->mlfq_priority];
    if (queue->count == UINT32_MAX)
        return false;

    thread->next = NULL;

    if (queue->tail)
        queue->tail->next = thread;
    else
        queue->head = thread;

    queue->tail = thread;
    queue->count++;
    scheduler->runnable_count++;

    return true;
}

bool hellcore_mlfq_enqueue(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread,
    uint64_t now_ns
) {
    if (!scheduler || !thread ||
        thread->sched_class != HELLCORE_SCHED_CLASS_MLFQ ||
        (thread->state != HELLCORE_THREAD_NEW &&
         thread->state != HELLCORE_THREAD_BLOCKED) ||
        thread->next != NULL ||
        thread == scheduler->current ||
        thread->mlfq_priority >= HELLCORE_MLFQ_LEVELS) {
        return false;
    }

    HELLCORE_THREAD_STATE old_state = thread->state;
    uint64_t old_wait_start = thread->mlfq_wait_start_ns;

    thread->state = HELLCORE_THREAD_RUNNABLE;
    thread->mlfq_wait_start_ns = now_ns;

    if (!mlfq_append(scheduler, thread)) {
        thread->state = old_state;
        thread->mlfq_wait_start_ns = old_wait_start;
        return false;
    }

    return true;
}

HELLCORE_SCHED_THREAD *hellcore_mlfq_pick(
    HELLCORE_SCHEDULER *scheduler
) {
    if (!scheduler || scheduler->current)
        return NULL;

    for (uint32_t i = 0; i < HELLCORE_MLFQ_LEVELS; i++) {
        HELLCORE_SCHED_QUEUE *queue = &scheduler->mlfq[i];
        HELLCORE_SCHED_THREAD *thread = queue->head;

        if (!thread)
            continue;

        if (queue->count == 0 ||
            scheduler->runnable_count == 0 ||
            !queue->tail ||
            thread->sched_class != HELLCORE_SCHED_CLASS_MLFQ ||
            thread->state != HELLCORE_THREAD_RUNNABLE ||
            thread->mlfq_priority != i ||
            (queue->count == 1 &&
             (thread->next != NULL || queue->tail != thread)) ||
            (queue->count > 1 &&
             (thread->next == NULL || queue->tail == thread))) {
            return NULL;
        }

        queue->head = thread->next;

        if (!queue->head)
            queue->tail = NULL;

        queue->count--;
        scheduler->runnable_count--;

        thread->next = NULL;
        thread->state = HELLCORE_THREAD_RUNNING;
        scheduler->current = thread;

        return thread;
    }

    return NULL;
}

bool hellcore_mlfq_requeue_expired(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread,
    uint64_t now_ns
) {
    if (!scheduler || !thread ||
        scheduler->current != thread ||
        thread->state != HELLCORE_THREAD_RUNNING ||
        thread->sched_class != HELLCORE_SCHED_CLASS_MLFQ ||
        thread->mlfq_priority >= HELLCORE_MLFQ_LEVELS) {
        return false;
    }

    uint8_t old_priority = thread->mlfq_priority;
    uint64_t old_quantum_used = thread->mlfq_quantum_used_ns;
    uint64_t old_wait_start = thread->mlfq_wait_start_ns;

    if (thread->mlfq_priority < HELLCORE_MLFQ_LEVELS - 1)
        thread->mlfq_priority++;

    thread->mlfq_quantum_used_ns = 0;
    thread->mlfq_wait_start_ns = now_ns;
    thread->state = HELLCORE_THREAD_RUNNABLE;
    scheduler->current = NULL;

    if (!mlfq_append(scheduler, thread)) {
        thread->mlfq_priority = old_priority;
        thread->mlfq_quantum_used_ns = old_quantum_used;
        thread->mlfq_wait_start_ns = old_wait_start;
        thread->state = HELLCORE_THREAD_RUNNING;
        scheduler->current = thread;
        return false;
    }

    return true;
}


bool hellcore_mlfq_requeue_yield(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread,
    uint64_t now_ns
) {
    if (!scheduler || !thread ||
        scheduler->current != thread ||
        thread->state != HELLCORE_THREAD_RUNNING ||
        thread->sched_class != HELLCORE_SCHED_CLASS_MLFQ ||
        thread->mlfq_priority >= HELLCORE_MLFQ_LEVELS ||
        scheduler->runnable_count == UINT32_MAX ||
        scheduler->mlfq[thread->mlfq_priority].count == UINT32_MAX ||
        thread->next != NULL) {
        return false;
    }

    uint64_t old_wait_start = thread->mlfq_wait_start_ns;

    thread->state = HELLCORE_THREAD_RUNNABLE;
    thread->mlfq_wait_start_ns = now_ns;
    scheduler->current = NULL;

    if (!mlfq_append(scheduler, thread)) {
        thread->state = HELLCORE_THREAD_RUNNING;
        thread->mlfq_wait_start_ns = old_wait_start;
        scheduler->current = thread;
        return false;
    }

    return true;
}

void hellcore_mlfq_age(
    HELLCORE_SCHEDULER *scheduler,
    uint64_t now_ns
) {
    if (!scheduler || scheduler->aging_interval_ns == 0)
        return;

    for (uint32_t level = 1; level < HELLCORE_MLFQ_LEVELS; level++) {
        HELLCORE_SCHED_QUEUE *source = &scheduler->mlfq[level];
        HELLCORE_SCHED_THREAD *previous = NULL;
        HELLCORE_SCHED_THREAD *thread = source->head;

        while (thread) {
            HELLCORE_SCHED_THREAD *next = thread->next;

            if (now_ns >= thread->mlfq_wait_start_ns &&
                now_ns - thread->mlfq_wait_start_ns >=
                    scheduler->aging_interval_ns &&
                scheduler->mlfq[level - 1].count < UINT32_MAX) {
                if (previous)
                    previous->next = next;
                else
                    source->head = next;

                if (source->tail == thread)
                    source->tail = previous;

                source->count--;

                thread->mlfq_priority--;
                thread->mlfq_wait_start_ns = now_ns;
                thread->next = NULL;

                HELLCORE_SCHED_QUEUE *destination =
                    &scheduler->mlfq[thread->mlfq_priority];

                if (destination->tail)
                    destination->tail->next = thread;
                else
                    destination->head = thread;

                destination->tail = thread;
                destination->count++;
            } else {
                previous = thread;
            }

            thread = next;
        }
    }
}
