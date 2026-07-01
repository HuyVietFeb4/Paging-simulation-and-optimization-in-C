CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include
LDFLAGS = -lpthread

EX1_BIN = ex1/ue ex1/gnodeb

# Define rules to compile everything
all: ex1

# Exercise 1 compilation
ex1: $(EX1_BIN)

ex1/ue: ex1/ue.c
	$(CC) $(CFLAGS) $< -o $@

ex1/gnodeb: ex1/gnodeb.c
	$(CC) $(CFLAGS) $< -o $@