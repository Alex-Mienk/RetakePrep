// epoll with UDP.
//
// Yes, epoll can be used with UDP sockets on Linux.
// UDP sockets are file descriptors, and epoll waits until a datagram is ready.
//
// Build on Linux:
//   cc -std=c17 -Wall -Wextra lab-epoll-udp.c -o lab-epoll-udp
//
// Run:
//   ./lab-epoll-udp 9000
//
// Send:
//   printf 'hello' | nc -u 127.0.0.1 9000

// #ifdef __linux__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 8
#define BUF_SIZE 128

static void die(const char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        die("fcntl get");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        die("fcntl set");
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0)
        die("socket");

    set_nonblocking(udp_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)atoi(argv[1]));

    if (bind(udp_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        die("bind");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
        die("epoll_create1");

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = udp_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &event) < 0)
        die("epoll_ctl add");

    printf("epoll waiting for UDP datagrams on port %s\n", argv[1]);

    for (;;) {
        struct epoll_event events[MAX_EVENTS];
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            die("epoll_wait");
        }

        for (int i = 0; i < ready; i++) {
            if (events[i].data.fd != udp_fd)
                continue;

            for (;;) {
                char buffer[BUF_SIZE + 1];
                struct sockaddr_in peer;
                socklen_t peer_len = sizeof(peer);

                ssize_t count = recvfrom(udp_fd, buffer, BUF_SIZE, 0,
                    (struct sockaddr*)&peer, &peer_len);

                if (count < 0) {
                    // Nonblocking socket: EAGAIN means all queued datagrams are read.
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    die("recvfrom");
                }

                buffer[count] = '\0';
                printf("datagram: %s\n", buffer);

                if (strcmp(buffer, "exit") == 0) {
                    close(epoll_fd);
                    close(udp_fd);
                    return 0;
                }
            }
        }
    }
}

// 

// #include <stdio.h>

// int main(void)
// {
//     printf("epoll is Linux-specific. Run this example on Linux.\n");
//     return 0;
// }

// #endif
