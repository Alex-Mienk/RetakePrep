#include "l8_common.h"

#include <sys/wait.h>

#define BACKLOG 16
#define LOGIN_MAX 16
#define COMMAND_MAX 8
#define PARAM_MAX 10
#define MIN_SIZE (LOGIN_MAX + COMMAND_MAX)
#define CMDS 6
#define WORKERS 2

static char* COMMANDS[CMDS] = {"RUN", "EXIT", "PAUSE", "COMPUTE", "LIST", "GATHER"};

typedef struct {
    char login[LOGIN_MAX];
    char command[COMMAND_MAX];
    uint32_t params[PARAM_MAX];
} message_t;

typedef struct {
    uint32_t count;
    uint32_t seed;
    int exit;
} job_t;

typedef struct {
    pid_t pid;
    uint32_t count;
    uint32_t seed;
    double value;
} result_t;

typedef struct {
    pid_t pid;
    int to_worker[2];     // parent writes jobs here, child reads from here
    int from_worker[2];   // child writes results here, parent reads from here
} worker_t;

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

int isValideMessage(message_t* message, int read_len)
{
    char login[LOGIN_MAX + 1];
    char command[COMMAND_MAX + 1];
    int isValidLogin = 0, isValidCMD = 0, isValidLen = 0;
    int login_len = 0, command_len = 0;

    if (read_len < MIN_SIZE || read_len > MSG_MAX) {
        printf("error: wrong message length %d\n", read_len);
        return 0;
    }

    while (login_len < LOGIN_MAX && message->login[login_len] != '\0')
        login_len++;
    while (command_len < COMMAND_MAX && message->command[command_len] != '\0')
        command_len++;

    memcpy(login, message->login, login_len);
    login[login_len] = '\0';
    memcpy(command, message->command, command_len);
    command[command_len] = '\0';

    for (int u = 0; u < USERS; u++)
        if (strcmp(login, LOGINS[u]) == 0)
            isValidLogin = 1;

    if (!isValidLogin) {
        printf("error: unknown user %s\n", login);
        return 0;
    }

    for (int c = 0; c < CMDS; c++)
        if (strcmp(command, COMMANDS[c]) == 0)
            isValidCMD = 1;

    if (!isValidCMD) {
        printf("error: unknown command %s\n", command);
        return 0;
    }

    if (strcmp(command, "COMPUTE") == 0) {
        if ((read_len - MIN_SIZE) > 0 && (read_len - MIN_SIZE) % 8 == 0)
            isValidLen = 1;
    } else if (read_len == MIN_SIZE) {
        isValidLen = 1;
    }

    if (!isValidLen) {
        printf("error: wrong message length %d\n", read_len);
        return 0;
    }

    return 1;
}

void copyFields(message_t* message, char* login, char* command)
{
    int login_len = 0, command_len = 0;

    while (login_len < LOGIN_MAX && message->login[login_len] != '\0')
        login_len++;
    while (command_len < COMMAND_MAX && message->command[command_len] != '\0')
        command_len++;

    memcpy(login, message->login, login_len);
    login[login_len] = '\0';
    memcpy(command, message->command, command_len);
    command[command_len] = '\0';
}

void workerLoop(int read_fd, int write_fd)
{
    for (;;) {
        job_t job;
        ssize_t count = TEMP_FAILURE_RETRY(read(read_fd, &job, sizeof(job)));
        if (count < 0)
            ERR("read");
        if (count == 0)
            break;
        if (count != sizeof(job))
            continue;
        if (job.exit)
            break;

        int seed = (int)job.seed;
        result_t result;
        result.pid = getpid();
        result.count = job.count;
        result.seed = job.seed;
        result.value = compute_pi((int)job.count, &seed);

        if (TEMP_FAILURE_RETRY(write(write_fd, &result, sizeof(result))) != sizeof(result))
            ERR("write");
    }

    close(read_fd);
    close(write_fd);
    exit(EXIT_SUCCESS);
}

void startWorker(worker_t* worker)
{
    if (pipe(worker->to_worker) < 0)
        ERR("pipe");
    if (pipe(worker->from_worker) < 0)
        ERR("pipe");

    worker->pid = fork();
    if (worker->pid < 0)
        ERR("fork");

    if (worker->pid == 0) {
        close(worker->to_worker[1]);
        close(worker->from_worker[0]);
        workerLoop(worker->to_worker[0], worker->from_worker[1]);
    }

    close(worker->to_worker[0]);
    close(worker->from_worker[1]);

    // Parent reads results only when GATHER arrives, so do not block there.
    int flags = fcntl(worker->from_worker[0], F_GETFL, 0);
    if (flags < 0)
        ERR("fcntl");
    if (fcntl(worker->from_worker[0], F_SETFL, flags | O_NONBLOCK) < 0)
        ERR("fcntl");
}

int main(int argc, char** argv)
{
    if (argc != 2)
        usage(argv[0]);

    worker_t workers[WORKERS];
    for (int w = 0; w < WORKERS; w++)
        startWorker(&workers[w]);

    int next_worker = 0;
    int socketfd = bind_inet_socket(atoi(argv[1]), SOCK_DGRAM, BACKLOG);

    for (;;) {
        message_t message;
        char login[LOGIN_MAX + 1];
        char command[COMMAND_MAX + 1];
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);

        memset(&message, 0, sizeof(message));
        int count = recvfrom(socketfd, &message, sizeof(message), 0, (struct sockaddr*)&addr, &len);
        if (count < 0)
            ERR("recvfrom");

        if (!isValideMessage(&message, count))
            continue;

        copyFields(&message, login, command);
        printf("%s: %s", login, command);

        for (int p = 0; p < (count - MIN_SIZE) / 4; p++) {
            message.params[p] = ntohl(message.params[p]);
            printf(" %u", message.params[p]);
        }
        printf("\n");

        if (strcmp(command, "COMPUTE") == 0) {
            // Each pair is one job: params[p] = count, params[p + 1] = seed.
            for (int p = 0; p < (count - MIN_SIZE) / 4; p += 2) {
                job_t job;
                job.count = message.params[p];
                job.seed = message.params[p + 1];
                job.exit = 0;

                int fd = workers[next_worker].to_worker[1];
                if (TEMP_FAILURE_RETRY(write(fd, &job, sizeof(job))) != sizeof(job))
                    ERR("write");

                next_worker = (next_worker + 1) % WORKERS;
            }
        } else if (strcmp(command, "GATHER") == 0) {
            // Read every result that is already available from every worker.
            for (int w = 0; w < WORKERS; w++) {
                for (;;) {
                    result_t result;
                    ssize_t result_count = read(workers[w].from_worker[0], &result, sizeof(result));

                    if (result_count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                        break;
                    if (result_count < 0)
                        ERR("read");
                    if (result_count == 0)
                        break;
                    if (result_count == sizeof(result))
                        printf("result from %d: count=%u seed=%u pi=%f\n",
                            result.pid, result.count, result.seed, result.value);
                }
            }
        } else if (strcmp(command, "EXIT") == 0) {
            break;
        }
    }

    // Tell child processes to stop, then close descriptors and reap them.
    for (int w = 0; w < WORKERS; w++) {
        job_t job;
        job.count = 0;
        job.seed = 0;
        job.exit = 1;
        if (TEMP_FAILURE_RETRY(write(workers[w].to_worker[1], &job, sizeof(job))) != sizeof(job))
            ERR("write");
        close(workers[w].to_worker[1]);
        close(workers[w].from_worker[0]);
        waitpid(workers[w].pid, NULL, 0);
    }

    close(socketfd);
    return 0;
}
