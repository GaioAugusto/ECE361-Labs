# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -pedantic -std=c11

# Targets
all: deliver server

deliver: deliver.c
	$(CC) $(CFLAGS) -o deliver deliver.c

server: server.c
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f deliver server