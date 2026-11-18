CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wno-unused-function
CFLAGS += -DHELLCORE_VMM_HOST_TEST

TARGET = test_vmm_user

.PHONY: all test clean

all: $(TARGET)

$(TARGET): test_vmm_user.c
	$(CC) $(CFLAGS) $< -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
