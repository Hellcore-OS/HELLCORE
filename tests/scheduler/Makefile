CC := clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -I../../kernel/sched

BUILD_DIR := ../../build/scheduler-check

MLFQ_SOURCES := test_mlfq.c ../../kernel/sched/sched_core.c ../../kernel/sched/mlfq.c
CFS_SOURCES := test_cfs.c ../../kernel/sched/sched_core.c ../../kernel/sched/cfs.c
POLICY_SOURCES := test_policy.c ../../kernel/sched/sched_core.c ../../kernel/sched/mlfq.c ../../kernel/sched/cfs.c ../../kernel/sched/sched_policy.c

.PHONY: all test clean

all: test

test: $(BUILD_DIR)/test_mlfq $(BUILD_DIR)/test_cfs $(BUILD_DIR)/test_policy
	$(BUILD_DIR)/test_mlfq
	$(BUILD_DIR)/test_cfs
	$(BUILD_DIR)/test_policy

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_mlfq: $(MLFQ_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(MLFQ_SOURCES) -o $@

$(BUILD_DIR)/test_cfs: $(CFS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CFS_SOURCES) -o $@

$(BUILD_DIR)/test_policy: $(POLICY_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(POLICY_SOURCES) -o $@

clean:
	rm -f $(BUILD_DIR)/test_mlfq $(BUILD_DIR)/test_cfs $(BUILD_DIR)/test_policy
