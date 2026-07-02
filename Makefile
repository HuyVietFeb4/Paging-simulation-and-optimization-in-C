CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include -I./ex1
LDFLAGS = -lpthread

EX1_BIN = ex1/ue ex1/gnodeb

# Define rules to compile everything
all: ex1

# Exercise 1 compilation
ex1: $(EX1_BIN)

# Links both ue.c and packet.c together
ex1/ue: ex1/ue.c ex1/packet.c
	$(CC) $(CFLAGS) $^ -o $@

# Links both gnodeb.c and packet.c together
ex1/gnodeb: ex1/gnodeb.c ex1/packet.c
	$(CC) $(CFLAGS) $^ -o $@

# Clean rule to clear out binaries before a fresh build
clean:
	rm -f $(EX1_BIN)

.PHONY: all ex1 clean