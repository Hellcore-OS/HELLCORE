#ifndef HELLCORE_SCHED_H
#define HELLCORE_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HELLCORE_MLFQ_LEVELS 4
#define HELLCORE_MLFQ_QUANTUM_NS(level) \
    (2000000ULL << (level))

#define HELLCORE_CFS_BASE_WEIGHT 1024U
#define HELLCORE_SCHED_DEFAULT_MLFQ_SHARE 30U
#define HELLCORE_SCHED_DEFAULT_CFS_SHARE 70U

typedef enum {
    HELLCORE_SCHED_CLASS_MLFQ = 0,
    HELLCORE_SCHED_CLASS_CFS = 1
} HELLCORE_SCHED_CLASS;

typedef enum {
    HELLCORE_THREAD_NEW = 0,
    HELLCORE_THREAD_RUNNABLE,
    HELLCORE_THREAD_RUNNING,
    HELLCORE_THREAD_BLOCKED,
    HELLCORE_THREAD_EXITED
} HELLCORE_THREAD_STATE;

typedef struct hellcore_sched_thread {
    uint64_t id;

    HELLCORE_SCHED_CLASS sched_class;
    HELLCORE_THREAD_STATE state;

    /* MLFQ accounting. */
    uint8_t mlfq_priority;
    uint64_t mlfq_quantum_used_ns;
    uint64_t mlfq_wait_start_ns;

    /* CFS accounting. */
    uint32_t weight;
    uint64_t vruntime;

    /* General accounting. */
    uint64_t total_runtime_ns;

    struct hellcore_sched_thread *next;
} HELLCORE_SCHED_THREAD;

typedef struct {
    HELLCORE_SCHED_THREAD *head;
    HELLCORE_SCHED_THREAD *tail;
    uint32_t count;
} HELLCORE_SCHED_QUEUE;

typedef struct {
    HELLCORE_SCHED_QUEUE mlfq[HELLCORE_MLFQ_LEVELS];

    /* CFS runnable list, ordered by vruntime. */
    HELLCORE_SCHED_THREAD *cfs_head;
    uint32_t cfs_count;

    HELLCORE_SCHED_THREAD *current;

    uint64_t class_runtime_ns[2];
    uint64_t cfs_min_vruntime;

    uint32_t mlfq_share;
    uint32_t cfs_share;

    /* Next class to prefer when normalized service is equal. */
    HELLCORE_SCHED_CLASS tie_break_next_class;

    /*
     * Competitive service counters. These are reset when the
     * set of runnable scheduling classes changes.
     */
    uint64_t last_aging_ns;
    uint64_t aging_interval_ns;

    uint32_t runnable_count;
} HELLCORE_SCHEDULER;

void hellcore_sched_init(
    HELLCORE_SCHEDULER *scheduler,
    uint32_t mlfq_share,
    uint32_t cfs_share,
    uint64_t aging_interval_ns
);

void hellcore_sched_thread_init(
    HELLCORE_SCHED_THREAD *thread,
    uint64_t id,
    HELLCORE_SCHED_CLASS sched_class,
    uint32_t weight,
    uint64_t now_ns
);

bool hellcore_sched_enqueue(
    HELLCORE_SCHEDULER *scheduler,
    HELLCORE_SCHED_THREAD *thread,
    uint64_t now_ns
);

HELLCORE_SCHED_THREAD *hellcore_sched_pick_next(
    HELLCORE_SCHEDULER *scheduler
);

bool hellcore_sched_account(
    HELLCORE_SCHEDULER *scheduler,
    uint64_t elapsed_ns
);

bool hellcore_sched_yield(
    HELLCORE_SCHEDULER *scheduler,
    uint64_t now_ns
);

bool hellcore_sched_block_current(
    HELLCORE_SCHEDULER *scheduler
);

bool hellcore_sched_exit_current(
    HELLCORE_SCHEDULER *scheduler
);

void hellcore_sched_age(
    HELLCORE_SCHEDULER *scheduler,
    uint64_t now_ns
);

#endif
