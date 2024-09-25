#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <stdbool.h>
#include <pthread.h>

static bool is_unix_socket = false;
static int num_threads = 1;
static int tcp_port = 8080;
static int packet_size = 5120;
static int packet_count = 50;

void* measure_performance(void* arg) {
    int thread_id = *(int*)arg;

    char* buffer = (char*)malloc(packet_size);
    if (!buffer) {
        perror("malloc failed");
        pthread_exit(NULL);
    }

    // Fill with random numbers
    for (int i = 0; i < packet_size; i++) {
        buffer[i] = '0' + (rand() % 10);
    }

    int client_socket;
    if (is_unix_socket) {
        if ((client_socket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
            perror("socket creation failed");
            free(buffer);
            pthread_exit(NULL);
        }

        struct sockaddr_un un_servaddr;
        un_servaddr.sun_family = AF_UNIX;
        strcpy(un_servaddr.sun_path, "/tmp/unix_socket");

        if (connect(client_socket, (struct sockaddr *)&un_servaddr, sizeof(un_servaddr)) < 0) {
            perror("connect failed");
            close(client_socket);
            free(buffer);
            pthread_exit(NULL);
        }

        printf("Thread %d: Connected to UNIX domain socket server.\n", thread_id);
    } else {
        if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket creation failed");
            free(buffer);
            pthread_exit(NULL);
        }

        struct sockaddr_in tcp_servaddr;
        tcp_servaddr.sin_family = AF_INET;
        tcp_servaddr.sin_addr.s_addr = INADDR_ANY;
        tcp_servaddr.sin_port = htons(tcp_port);

        if (connect(client_socket, (struct sockaddr *)&tcp_servaddr, sizeof(tcp_servaddr)) < 0) {
            perror("connect failed");
            close(client_socket);
            free(buffer);
            pthread_exit(NULL);
        }

        printf("Thread %d: Connected to TCP server on port %d.\n", thread_id, tcp_port);
    }

    for (int i = 0; i < packet_count; i++) {
        size_t bytes_sent = send(client_socket, buffer, packet_size, 0);
        if (bytes_sent < 0) {
            perror("send error");
            free(buffer);
            close(client_socket);
            pthread_exit(NULL);
        }

        size_t bytes_received = recv(client_socket, buffer, packet_size, 0);
        if (bytes_received < 0) {
            perror("recv error");
            free(buffer);
            close(client_socket);
            pthread_exit(NULL);
        }
    }

    free(buffer);
    close(client_socket);
    pthread_exit(NULL);
}

int main(void) {
    srand((int)time(NULL));

    pthread_t threads[num_threads];
    int thread_ids[num_threads];

    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i;
        int res = pthread_create(&threads[i], NULL, measure_performance, (void*)&thread_ids[i]);
        if (res) {
            fprintf(stderr, "Error creating thread %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
