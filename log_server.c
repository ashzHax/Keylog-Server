// server.c

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 5555
#define MAX_CLIENTS  FD_SETSIZE
#define BUFFER_SIZE 4096

int server_fd;
int client_fds[MAX_CLIENTS];

void cleanup(int sig) {
    printf("\nShutting down server...[%d]\n", sig);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] > 0)
            close(client_fds[i]);
    }
    close(server_fd);
    exit(0);
}

void ensure_data_directory() {
    struct stat st = {0};
    if (stat("data", &st) == -1) {
        mkdir("data", 0755);
    }
}

void remove_client(int index) {
    close(client_fds[index]);
    client_fds[index] = 0;
}

void save_log(char *timestamp, char *ip, char *hostname, char *data) {
    char date[10];
    strncpy(date, timestamp, 10);
    date[4] = date[7] = '-';
    date[9] = '\0';

    // Convert YYYY-MM-DD → YYYYMMDD
    char formatted_date[9];
    snprintf(formatted_date, sizeof(formatted_date),
             "%.4s%.2s%.2s",
             timestamp,
             timestamp + 5,
             timestamp + 8);

    char filename[512];
    snprintf(filename, sizeof(filename),
             "data/%s_%s_%s.log",
             formatted_date, ip, hostname);

    FILE *fp = fopen(filename, "a");
    if (!fp) {
        perror("fopen");
        return;
    }

    fprintf(fp, "%s|%s|%s|%s\n",
            timestamp, ip, hostname, data);

    fclose(fp);
}

void process_message(char *message) {
    char *timestamp = strtok(message, "|");
    char *ip = strtok(NULL, "|");
    char *hostname = strtok(NULL, "|");
    char *data = strtok(NULL, "\n");

    if (!timestamp || !ip || !hostname || !data)
        return;

    save_log(timestamp, ip, hostname, data);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    fd_set readfds;
    socklen_t addrlen;
    int max_sd, activity;

    signal(SIGINT, cleanup);

    ensure_data_directory();

    memset(client_fds, 0, sizeof(client_fds));

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_fds[i];
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_sd)
                max_sd = sd;
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }

        // New connection
        if (FD_ISSET(server_fd, &readfds)) {
            addrlen = sizeof(client_addr);
            int new_socket = accept(server_fd,
                                    (struct sockaddr *)&client_addr,
                                    &addrlen);
            if (new_socket < 0) {
                perror("accept");
                continue;
            }

            printf("New connection: %s:%d\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_fds[i] == 0) {
                    client_fds[i] = new_socket;
                    break;
                }
            }
        }

        // Existing clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_fds[i];

            if (FD_ISSET(sd, &readfds)) {
                char buffer[BUFFER_SIZE];
                int valread = recv(sd, buffer, BUFFER_SIZE - 1, 0);

                if (valread <= 0) {
                    printf("Client disconnected (fd=%d)\n", sd);
                    remove_client(i);
                } else {
                    buffer[valread] = '\0';

                    char *line = strtok(buffer, "\n");
                    while (line != NULL) {
                        process_message(line);
                        line = strtok(NULL, "\n");
                    }
                }
            }
        }
    }

    return 0;
}

