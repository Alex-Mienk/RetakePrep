// mock_stage1_2_3.c
// Stage 1: UDP server receives datagrams
// Stage 2: parse binary header + string
// Stage 3: fork + pipe + mmap shared memory

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define ERR(source) \
    (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define MAX_TEXT 256
#define MAX_BUF 512

typedef struct {
    uint32_t command;
    uint32_t value;
    uint32_t text_len;
} packet_header_t;

typedef struct {
    uint32_t command;
    uint32_t value;
    char text[MAX_TEXT];
} request_t;

// STAGE 3:
// Shared memory visible to parent and child.
typedef struct {
    int counter;
    int last_result;
    char last_text[MAX_TEXT];
} shared_t;

// STAGE 1:
int make_udp_socket(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        ERR("socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");

    return fd;
}

// STAGE 2:
int parse_packet(char *buf, ssize_t len, request_t *req)
{
    if (len < (ssize_t)sizeof(packet_header_t))
        return -1;

    packet_header_t header;
    memcpy(&header, buf, sizeof(header));

    req->command = ntohl(header.command);
    req->value = ntohl(header.value);

    uint32_t text_len = ntohl(header.text_len);

    if (text_len >= MAX_TEXT)
        return -1;

    if (len < (ssize_t)(sizeof(packet_header_t) + text_len))
        return -1;

    memcpy(req->text, buf + sizeof(packet_header_t), text_len);
    req->text[text_len] = '\0';

    return 0;
}

// STAGE 3:
// Create anonymous shared memory before fork().
shared_t *create_shared(void)
{
    shared_t *shared = mmap(
        NULL,
        sizeof(shared_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (shared == MAP_FAILED)
        ERR("mmap");

    shared->counter = 0;
    shared->last_result = 0;
    shared->last_text[0] = '\0';

    return shared;
}

// STAGE 3:
// Child process reads request_t from pipe and writes result to mmap.
void worker_loop(int pipe_read_fd, shared_t *shared)
{
    for (;;) {
        request_t req;

        ssize_t bytes = read(pipe_read_fd, &req, sizeof(req));

        if (bytes == 0)
            break;

        if (bytes < 0)
            ERR("read");

        int result = req.value * 2;

        shared->counter++;
        shared->last_result = result;

        strncpy(shared->last_text, req.text, MAX_TEXT - 1);
        shared->last_text[MAX_TEXT - 1] = '\0';

        printf("[worker] value=%u result=%d text=%s\n",
               req.value, result, req.text);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // STAGE 3:
    // mmap must be created before fork().
    shared_t *shared = create_shared();

    // STAGE 3:
    // Pipe sends parsed request_t from parent to child.
    int pipe_fd[2];

    if (pipe(pipe_fd) < 0)
        ERR("pipe");

    pid_t pid = fork();

    if (pid < 0)
        ERR("fork");

    if (pid == 0) {
        // Child = worker.
        close(pipe_fd[1]); // child does not write

        worker_loop(pipe_fd[0], shared);

        close(pipe_fd[0]);
        exit(EXIT_SUCCESS);
    }

    // Parent = UDP server.
    close(pipe_fd[0]); // parent does not read

    // STAGE 1:
    int udp_fd = make_udp_socket((uint16_t)atoi(argv[1]));

    printf("UDP server started on port %s\n", argv[1]);

    for (;;) {
        char buf[MAX_BUF];

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // STAGE 1:
        // Receive one whole UDP datagram.
        ssize_t len = recvfrom(
            udp_fd,
            buf,
            sizeof(buf),
            0,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (len < 0)
            ERR("recvfrom");

        request_t req;
        memset(&req, 0, sizeof(req));

        char response[256];

        // STAGE 2:
        // Parse binary datagram into request_t.
        if (parse_packet(buf, len, &req) < 0) {
            snprintf(response, sizeof(response), "Invalid packet");
        } else {
            // STAGE 3:
            // Send parsed request to worker through pipe.
            if (write(pipe_fd[1], &req, sizeof(req)) < 0)
                ERR("write");

            snprintf(
                response,
                sizeof(response),
                "sent_to_worker counter=%d last_result=%d last_text=%s",
                shared->counter,
                shared->last_result,
                shared->last_text
            );
        }

        // STAGE 1:
        // Send response back to UDP client.
        sendto(
            udp_fd,
            response,
            strlen(response),
            0,
            (struct sockaddr *)&client_addr,
            client_len
        );
    }

    close(udp_fd);
    close(pipe_fd[1]);

    wait(NULL);

    munmap(shared, sizeof(shared_t));

    return 0;
}