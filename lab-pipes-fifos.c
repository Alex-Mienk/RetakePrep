// Basic pipes and FIFOs.
//
// Anonymous pipe:
//   - created with pipe(fd)
//   - used between related processes after fork()
//
// FIFO:
//   - created with mkfifo(path)
//   - has a filesystem name
//   - can be opened by unrelated processes
//
// Build:
//   cc -std=c17 -Wall -Wextra lab-pipes-fifos.c -o lab-pipes-fifos
//
// Run:
//   ./lab-pipes-fifos pipe
//   ./lab-pipes-fifos fifo-read
//   ./lab-pipes-fifos fifo-write

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define FIFO_PATH "/tmp/ops_lab_fifo"

static void die(const char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void run_pipe_example(void)
{
    int fd[2];
    if (pipe(fd) < 0)
        die("pipe");

    pid_t pid = fork();
    if (pid < 0)
        die("fork");

    if (pid == 0) {
        // Child reads from fd[0]. It does not write.
        close(fd[1]);

        int value;
        ssize_t count = read(fd[0], &value, sizeof(value));
        if (count < 0)
            die("read");
        if (count == sizeof(value))
            printf("child read value: %d\n", value);

        close(fd[0]);
        exit(EXIT_SUCCESS);
    }

    // Parent writes to fd[1]. It does not read.
    close(fd[0]);

    int value = 1234;
    if (write(fd[1], &value, sizeof(value)) != sizeof(value))
        die("write");

    close(fd[1]);
    waitpid(pid, NULL, 0);
}

static void run_fifo_reader(void)
{
    if (mkfifo(FIFO_PATH, 0600) < 0 && errno != EEXIST)
        die("mkfifo");

    printf("opening FIFO for reading: %s\n", FIFO_PATH);
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd < 0)
        die("open read");

    int value;
    ssize_t count = read(fd, &value, sizeof(value));
    if (count < 0)
        die("read fifo");
    if (count == sizeof(value))
        printf("fifo reader got value: %d\n", value);

    close(fd);
}

static void run_fifo_writer(void)
{
    if (mkfifo(FIFO_PATH, 0600) < 0 && errno != EEXIST)
        die("mkfifo");

    printf("opening FIFO for writing: %s\n", FIFO_PATH);
    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd < 0)
        die("open write");

    int value = 5678;
    if (write(fd, &value, sizeof(value)) != sizeof(value))
        die("write fifo");

    close(fd);
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s pipe|fifo-read|fifo-write\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "pipe") == 0)
        run_pipe_example();
    else if (strcmp(argv[1], "fifo-read") == 0)
        run_fifo_reader();
    else if (strcmp(argv[1], "fifo-write") == 0)
        run_fifo_writer();
    else {
        fprintf(stderr, "unknown mode: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    return 0;
}
