#include "sched.h"
#include "mlfq.h"
#include "cfs.h"

#define CLASS_BIT(c) (1U << (c))

static uint32_t runnable_mask(const HELLCORE_SCHEDULER *s)
{
    uint32_t mask = 0;

    if (!s)
        return 0;

    for (uint32_t i = 0; i < HELLCORE_MLFQ_LEVELS; i++) {
        if (s->mlfq[i].head) {
            mask |= CLASS_BIT(HELLCORE_SCHED_CLASS_MLFQ);
            break;
        }
    }

    if (s->cfs_head)
        mask |= CLASS_BIT(HELLCORE_SCHED_CLASS_CFS);

    if (s->current) {
        if (s->current->sched_class == HELLCORE_SCHED_CLASS_MLFQ)
            mask |= CLASS_BIT(HELLCORE_SCHED_CLASS_MLFQ);
        else if (s->current->sched_class == HELLCORE_SCHED_CLASS_CFS)
            mask |= CLASS_BIT(HELLCORE_SCHED_CLASS_CFS);
    }

    return mask;
}

static bool scheduler_config_valid(const HELLCORE_SCHEDULER *s)
{
    return s &&
        s->mlfq_share > 0 &&
        s->cfs_share > 0 &&
        s->mlfq_share <= 100 &&
        s->cfs_share <= 100 &&
        s->mlfq_share + s->cfs_share == 100 &&
        (s->tie_break_next_class == HELLCORE_SCHED_CLASS_MLFQ ||
         s->tie_break_next_class == HELLCORE_SCHED_CLASS_CFS);
}

static void reset_service(HELLCORE_SCHEDULER *s)
{
    s->class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] = 0;
    s->class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] = 0;
}

static void sync_availability(
    HELLCORE_SCHEDULER *s,
    uint32_t previous
)
{
    if (previous != runnable_mask(s))
        reset_service(s);
}

static bool class_runnable(
    const HELLCORE_SCHEDULER *s,
    HELLCORE_SCHED_CLASS c
)
{
    if (c == HELLCORE_SCHED_CLASS_CFS)
        return s->cfs_head != NULL;

    if (c == HELLCORE_SCHED_CLASS_MLFQ) {
        for (uint32_t i = 0; i < HELLCORE_MLFQ_LEVELS; i++) {
            if (s->mlfq[i].head)
                return true;
        }
    }

    return false;
}

/*
 * Compare normalized service without multiplying full-width
 * runtime counters by class shares.
 */
static int compare_service(const HELLCORE_SCHEDULER *s)
{
    uint64_t mr =
        s->class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ];
    uint64_t cr =
        s->class_runtime_ns[HELLCORE_SCHED_CLASS_CFS];

    uint32_t ms = s->mlfq_share;
    uint32_t cs = s->cfs_share;

    uint64_t mq = mr / ms;
    uint64_t cq = cr / cs;

    if (mq < cq)
        return -1;
    if (mq > cq)
        return 1;

    uint64_t left = (mr % ms) * cs;
    uint64_t right = (cr % cs) * ms;

    if (left < right)
        return -1;
    if (left > right)
        return 1;

    return 0;
}

bool hellcore_sched_enqueue(
    HELLCORE_SCHEDULER *s,
    HELLCORE_SCHED_THREAD *t,
    uint64_t now_ns
)
{
    if (!scheduler_config_valid(s) || !t)
        return false;

    uint32_t previous = runnable_mask(s);
    bool result;

    if (t->sched_class == HELLCORE_SCHED_CLASS_MLFQ) {
        result = hellcore_mlfq_enqueue(s, t, now_ns);
    } else if (t->sched_class == HELLCORE_SCHED_CLASS_CFS) {
        result = hellcore_cfs_enqueue(s, t);
    } else {
        return false;
    }

    if (result)
        sync_availability(s, previous);

    return result;
}

HELLCORE_SCHED_THREAD *hellcore_sched_pick_next(
    HELLCORE_SCHEDULER *s
)
{
    if (!scheduler_config_valid(s) || s->current)
        return NULL;

    bool m = class_runnable(s, HELLCORE_SCHED_CLASS_MLFQ);
    bool c = class_runnable(s, HELLCORE_SCHED_CLASS_CFS);

    if (!m && !c)
        return NULL;

    if (m && !c)
        return hellcore_mlfq_pick(s);

    if (c && !m)
        return hellcore_cfs_pick(s);

    int comparison = compare_service(s);
    HELLCORE_SCHED_CLASS selected;
    HELLCORE_SCHED_CLASS next_tie_break = s->tie_break_next_class;
    bool update_tie_break = false;

    if (comparison < 0) {
        selected = HELLCORE_SCHED_CLASS_MLFQ;
    } else if (comparison > 0) {
        selected = HELLCORE_SCHED_CLASS_CFS;
    } else {
        selected = s->tie_break_next_class;
        next_tie_break =
            selected == HELLCORE_SCHED_CLASS_MLFQ
                ? HELLCORE_SCHED_CLASS_CFS
                : HELLCORE_SCHED_CLASS_MLFQ;
        update_tie_break = true;
    }

    HELLCORE_SCHED_THREAD *thread =
        selected == HELLCORE_SCHED_CLASS_MLFQ
            ? hellcore_mlfq_pick(s)
            : hellcore_cfs_pick(s);

    if (thread && update_tie_break)
        s->tie_break_next_class = next_tie_break;

    return thread;
}

bool hellcore_sched_account(
    HELLCORE_SCHEDULER *s,
    uint64_t elapsed_ns
)
{
    if (!s || !s->current)
        return false;

    HELLCORE_SCHED_THREAD *t = s->current;

    if (t->state != HELLCORE_THREAD_RUNNING)
        return false;

    if (t->sched_class != HELLCORE_SCHED_CLASS_MLFQ &&
        t->sched_class != HELLCORE_SCHED_CLASS_CFS)
        return false;

    if (!scheduler_config_valid(s))
        return false;

    uint32_t mask = runnable_mask(s);

    bool competing =
        (mask & CLASS_BIT(HELLCORE_SCHED_CLASS_MLFQ)) &&
        (mask & CLASS_BIT(HELLCORE_SCHED_CLASS_CFS));

    uint64_t new_mlfq =
        s->class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ];
    uint64_t new_cfs =
        s->class_runtime_ns[HELLCORE_SCHED_CLASS_CFS];

    if (competing) {
        uint64_t *runtime =
            t->sched_class == HELLCORE_SCHED_CLASS_MLFQ
                ? &new_mlfq : &new_cfs;

        if (*runtime > UINT64_MAX - elapsed_ns) {
            /*
             * Remove a common normalized-service baseline.
             * This preserves the exact comparison between classes.
             */
            uint64_t m_base = new_mlfq / s->mlfq_share;
            uint64_t c_base = new_cfs / s->cfs_share;
            uint64_t baseline =
                m_base < c_base ? m_base : c_base;

            new_mlfq -= baseline * s->mlfq_share;
            new_cfs -= baseline * s->cfs_share;
        }

        runtime =
            t->sched_class == HELLCORE_SCHED_CLASS_MLFQ
                ? &new_mlfq : &new_cfs;

        if (*runtime > UINT64_MAX - elapsed_ns)
            return false;

        *runtime += elapsed_ns;
    }

    if (t->sched_class == HELLCORE_SCHED_CLASS_MLFQ) {
        if (t->mlfq_quantum_used_ns >
                UINT64_MAX - elapsed_ns ||
            t->total_runtime_ns >
                UINT64_MAX - elapsed_ns)
            return false;

        t->mlfq_quantum_used_ns += elapsed_ns;
        t->total_runtime_ns += elapsed_ns;
    } else {
        if (!hellcore_cfs_account_current(s, elapsed_ns))
            return false;
    }

    if (competing) {
        s->class_runtime_ns[HELLCORE_SCHED_CLASS_MLFQ] =
            new_mlfq;
        s->class_runtime_ns[HELLCORE_SCHED_CLASS_CFS] =
            new_cfs;
    }

    return true;
}

bool hellcore_sched_yield(
    HELLCORE_SCHEDULER *s,
    uint64_t now_ns
)
{
    if (!s || !s->current)
        return false;

    uint32_t previous = runnable_mask(s);
    HELLCORE_SCHED_THREAD *t = s->current;
    bool result;

    if (t->sched_class == HELLCORE_SCHED_CLASS_MLFQ) {
        result = hellcore_mlfq_requeue_yield(s, t, now_ns);
    } else if (t->sched_class == HELLCORE_SCHED_CLASS_CFS) {
        result = hellcore_cfs_requeue_current(s);
    } else {
        return false;
    }

    if (result)
        sync_availability(s, previous);

    return result;
}

bool hellcore_sched_block_current(HELLCORE_SCHEDULER *s)
{
    if (!s || !s->current)
        return false;

    HELLCORE_SCHED_THREAD *t = s->current;

    if (t->state != HELLCORE_THREAD_RUNNING ||
        t->next != NULL ||
        (t->sched_class != HELLCORE_SCHED_CLASS_MLFQ &&
         t->sched_class != HELLCORE_SCHED_CLASS_CFS))
        return false;

    uint32_t previous = runnable_mask(s);

    t->state = HELLCORE_THREAD_BLOCKED;
    s->current = NULL;

    sync_availability(s, previous);
    return true;
}

bool hellcore_sched_exit_current(HELLCORE_SCHEDULER *s)
{
    if (!s || !s->current)
        return false;

    HELLCORE_SCHED_THREAD *t = s->current;

    if (t->state != HELLCORE_THREAD_RUNNING ||
        t->next != NULL ||
        (t->sched_class != HELLCORE_SCHED_CLASS_MLFQ &&
         t->sched_class != HELLCORE_SCHED_CLASS_CFS))
        return false;

    uint32_t previous = runnable_mask(s);

    t->state = HELLCORE_THREAD_EXITED;
    s->current = NULL;

    sync_availability(s, previous);
    return true;
}

void hellcore_sched_age(
    HELLCORE_SCHEDULER *s,
    uint64_t now_ns
)
{
    if (!s || s->aging_interval_ns == 0)
        return;

    if (now_ns < s->last_aging_ns)
        return;

    if (now_ns - s->last_aging_ns < s->aging_interval_ns)
        return;

    hellcore_mlfq_age(s, now_ns);
    s->last_aging_ns = now_ns;
}
