// Basic UDP workflow: socket -> bind -> recvfrom.
//
// Build:
//   cc -std=c17 -Wall -Wextra lab-udp-workflow.c -o lab-udp-workflow
//
// Run server:
//   ./lab-udp-workflow 9000
//
// Send test datagram from another terminal:
//   printf 'hello udp' | nc -u 127.0.0.1 9000

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 128

static void die(const char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        die("socket");

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons((uint16_t)atoi(argv[1]));

    // bind chooses the local UDP port that clients send to.
    if (bind(socket_fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0)
        die("bind");

    printf("waiting for UDP datagrams on port %s\n", argv[1]);

    for (;;) {
        char buffer[BUF_SIZE + 1];
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);

        // recvfrom blocks until one whole UDP datagram arrives.
        ssize_t count = recvfrom(socket_fd, buffer, BUF_SIZE, 0,
            (struct sockaddr*)&peer_addr, &peer_len);
        if (count < 0)
            die("recvfrom");

        buffer[count] = '\0';

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer_addr.sin_addr, ip, sizeof(ip));
        printf("from %s:%u: %s\n", ip, ntohs(peer_addr.sin_port), buffer);

        if (strcmp(buffer, "exit") == 0)
            break;
    }

    close(socket_fd);
    return 0;
}
