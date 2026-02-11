all: log_server

log_server: log_server.c
	gcc -Wall -Wextra -O2 -pthread log_server.c -o log_server

debug: log_server.c
	gcc -Wall -Wextra -g -pthread log_server.c -o log_server_debug

clean:
	rm -f log_server log_server_debug

run: log_server
	./log_server
