#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PORT 5555
#define BUF_SIZE 4096
#define THREAD_POOL_SIZE 4
#define QUEUE_SIZE 64

// ================= Queue =================
int queue[QUEUE_SIZE];
int q_head = 0, q_tail = 0;

pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;

void enqueue(int client_sock) {
    pthread_mutex_lock(&q_mutex);
    queue[q_tail] = client_sock;
    q_tail = (q_tail + 1) % QUEUE_SIZE;
    pthread_cond_signal(&q_cond);
    pthread_mutex_unlock(&q_mutex);
}

int dequeue() {
    pthread_mutex_lock(&q_mutex);
    while (q_head == q_tail)
        pthread_cond_wait(&q_cond, &q_mutex);

    int sock = queue[q_head];
    q_head = (q_head + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&q_mutex);
    return sock;
}

// ================= Utilities =================
void ensure_data_dir() {
    struct stat st = {0};
    if (stat("data", &st) == -1)
        mkdir("data", 0700);
}

void get_date(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d", t);
}

void sanitize(char *s) {
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') ||
              (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') ||
              *s == '-' || *s == '_'))
            *s = '_';
    }
}

void extract_hostname(const char *msg, char *hostname, size_t size) {
    const char *key = "\"hostname\":\"";
    char *start = strstr(msg, key);
    if (!start) {
        snprintf(hostname, size, "unknown");
        return;
    }
    start += strlen(key);
    char *end = strchr(start, '"');
    if (!end) {
        snprintf(hostname, size, "unknown");
        return;
    }
    size_t len = end - start;
    if (len >= size) len = size - 1;
    strncpy(hostname, start, len);
    hostname[len] = '\0';
    sanitize(hostname);
}

// ================= Client Handler =================
void handle_client(int client_sock) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    // ipv4 related?
    getpeername(client_sock, (struct sockaddr *)&addr, &len);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));

    char buffer[BUF_SIZE];

    while (1) {
        ssize_t bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        buffer[bytes] = '\0';

        char hostname[256];
        extract_hostname(buffer, hostname, sizeof(hostname));

        char date[32];
        get_date(date, sizeof(date));

        char path[512];
        snprintf(path, sizeof(path), "data/%s_%s_%s.log", date, ip, hostname);

        FILE *f = fopen(path, "a");
        if (f) {
            flock(fileno(f), LOCK_EX);
            fprintf(f, "%s\n", buffer);
            fflush(f);
            flock(fileno(f), LOCK_UN);
            fclose(f);
        }
    }
    close(client_sock);
}

// ================= Worker Thread =================
void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        int client_sock = dequeue();
        handle_client(client_sock);
    }
    return NULL;
}

// ================= Main =================
int main() {
    ensure_data_dir();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        return 1;
    }

    printf("Log server listening on port %d with %d worker threads...\n",
           PORT, THREAD_POOL_SIZE);

    pthread_t workers[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_create(&workers[i], NULL, worker_thread, NULL);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);

        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        enqueue(client_sock);
    }

    close(server_fd);
    return 0;
}


