.PHONY: all clean

CC := gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic

all: code

code: main.c buddy.c buddy.h utils.h
	$(CC) $(CFLAGS) -o $@ main.c buddy.c

clean:
	rm -f code test *.o
