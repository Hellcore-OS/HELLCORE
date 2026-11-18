CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wno-unused-function
CFLAGS += -DHELLCORE_VMM_HOST_TEST

TARGET = test_vmm_rollback
SOURCE = test_vmm_rollback.c
DEPS = ../../kernel/vmm.c ../../kernel/vmm.h ../../kernel/pmm.h

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCE) $(DEPS)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
