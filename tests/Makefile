CC ?= cc
CFLAGS += -std=c11 -Wall -Wextra -Werror -O1 -g -I../main -fsanitize=address,undefined

test: test_core
	./test_core

test_core: test_core.c ../main/core.c ../main/core.h
	$(CC) $(CFLAGS) test_core.c ../main/core.c -o $@

clean:
	rm -f test_core

.PHONY: test clean
