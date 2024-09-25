#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <time.h>
#include <stdbool.h>

#define UNIX_SOCKETS 0
#define NON_BLOCKING 0

#define SOCKET_PATH "/tmp/unix_socket"
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024 * 1024
#define PORT 8080

#if UNIX_SOCKETS
    #define socketaddr sockaddr_un
#else
    #define socketaddr sockaddr_in
#endif

size_t total_bytes = 0;
size_t total_packets = 0;

struct timespec start_time, end_time;
bool start_time_initialized = false;

int set_non_blocking(int socket_fd) {
#if NON_BLOCKING
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return -1;
    }
#endif
    return 0;
}

void init_start_time(void)
{
    if (!start_time_initialized)
    {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        start_time_initialized = true;
    }
}

double get_elapsed_time(struct timespec *start, struct timespec *end) {
    double start_sec = (double)start->tv_sec + (double)start->tv_nsec / 1e9;
    double end_sec = (double)end->tv_sec + (double)end->tv_nsec / 1e9;
    return end_sec - start_sec;
}

void print_stats_stats(void) {
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_elapsed = get_elapsed_time(&start_time, &end_time);

    double bytes_per_sec = (double)total_bytes / total_elapsed;
    double packets_per_sec = (double)total_packets / total_elapsed;

    char* socket_type = UNIX_SOCKETS ? "UNIX" : "INET";
    char* blocking_type = NON_BLOCKING ? "non-blocking" : "blocking";
    printf("\n--- Performance Stats ---\n");
    printf("Socket type: %s, %s\n", socket_type, blocking_type);
    printf("Total Bytes: %zu\n", total_bytes);
    printf("Total Packets: %zu\n", total_packets);
    printf("Elapsed Time: %.6f seconds\n", total_elapsed);
    printf("Throughput bytes: %.2f bytes/sec\n", bytes_per_sec);
    printf("Throughput packets: %.2f packets/sec\n", packets_per_sec);
    printf("--------------------------------\n");
}

int create_server_socket(void) {
    int server_socket;
    int socket_type = UNIX_SOCKETS ? AF_UNIX : AF_INET;

    server_socket = socket(socket_type, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set server socket to non-blocking
    if (set_non_blocking(server_socket) == -1) {
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    return server_socket;
}

void prepare_server_address(struct socketaddr *addr) {
#if UNIX_SOCKETS
    addr->sun_family = AF_UNIX;
    strcpy(addr->sun_path, SOCKET_PATH);

    unlink(SOCKET_PATH);
#else
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons(PORT);
#endif
}

void bind_server_socket(int server_socket, struct socketaddr *addr) {
    if (bind(server_socket, (struct sockaddr*)addr, sizeof(*addr)) == -1) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
}

void listen_server_socket(int server_socket) {
    if (listen(server_socket, SOMAXCONN) == -1) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
}

int main() {
    int server_socket = create_server_socket();
    struct socketaddr server_addr;
    prepare_server_address(&server_addr);
    bind_server_socket(server_socket, &server_addr);
    listen_server_socket(server_socket);

    int client_sockets[MAX_CLIENTS] = {0};
    while (1) {
        struct socketaddr client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket != -1) {
            printf("New client %d connected\n", client_socket);

            if (set_non_blocking(client_socket) == -1) {
                close(client_socket);
                continue;
            }

            // Add new client socket to the list
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = client_socket;
                    break;
                }
            }
        }

        // Handle data from clients
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int client_socket = client_sockets[i];
            if (client_socket > 0) {
                while (1)
                {
                    char* buffer = (char*)malloc(BUFFER_SIZE);
                    long n = recv(client_socket, buffer, BUFFER_SIZE, 0);
                    if (n > 0) {
                        init_start_time();

                        total_bytes += n;
                        total_packets += 1;

                        // Echo message back to the client
                        if (send(client_socket, buffer, n, 0) == -1) {
                            perror("send");
                        }
#if NON_BLOCKING
                        break; // for non-blocking we don't we need to receive all data at once
#endif

                    } else if (n == 0 || (n == -1 && errno != EWOULDBLOCK && errno != EAGAIN)) {
                        printf("Client %d disconnected\n", client_socket);
                        close(client_socket);
                        client_sockets[i] = 0;
                        print_stats_stats();
                        break;
                    }
                }
            }
        }
    }
}
