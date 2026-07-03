CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include -I./ex1 -I./ex2
LDFLAGS = -lpthread

EX1_BIN = ex1/ue ex1/gnodeb
EX2_BIN = ex2/ue ex2/gnodeb ex2/amf

# Define rules to compile everything
all: ex1 ex2

# Exercise 1 compilation
ex1: $(EX1_BIN)

# Links both ue.c and packet.c together
ex1/ue: ex1/ue.c ex1/packet.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Links both gnodeb.c and packet.c together
ex1/gnodeb: ex1/gnodeb.c ex1/packet.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Exercise 2 compilation
ex2: $(EX2_BIN)

# Links ue.c, packet.c, and ue_registry.c together 
ex2/ue: ex2/ue.c ex2/packet.c ex2/ue_registry.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Links gnodeb.c and packet.c together
ex2/gnodeb: ex2/gnodeb.c ex2/packet.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Links amf.c, packet.c, and ue_registry.c together
ex2/amf: ex2/amf.c ex2/packet.c ex2/ue_registry.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Clean rule to clear out binaries before a fresh build
clean:
	rm -f $(EX1_BIN)
	rm -f $(EX2_BIN)

.PHONY: all ex1 ex2 clean