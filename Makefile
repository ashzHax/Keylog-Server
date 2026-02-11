CC=gcc
CFLAGS=-Wall -Wextra -O2

all: log_server

log_server: log_server.c
	$(CC) $(CFLAGS) log_server.c -o log_server

clean:
	rm -f log_server


