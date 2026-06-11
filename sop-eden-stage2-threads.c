#include "l8_common.h"

#define BACKLOG 16
#define LOGIN_MAX 16
#define COMMAND_MAX 8
#define PARAM_MAX 10
#define MIN_SIZE (LOGIN_MAX + COMMAND_MAX)
#define CMDS 6
#define JOB_QUEUE_SIZE 64
#define RESULT_QUEUE_SIZE 64

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
    int worker_id;
    uint32_t count;
    uint32_t seed;
    double value;
} result_t;

typedef struct {
    job_t jobs[JOB_QUEUE_SIZE];
    int head;
    int tail;
    pthread_mutex_t mutex;
    sem_t items;   // number of queued jobs
    sem_t slots;   // number of free job slots
} job_queue_t;

typedef struct {
    result_t results[RESULT_QUEUE_SIZE];
    int head;
    int tail;
    pthread_mutex_t mutex;
    sem_t items;   // number of queued results
    sem_t slots;   // number of free result slots
} result_queue_t;

static job_queue_t JOB_QUEUE = {
    .head = 0,
    .tail = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

static result_queue_t RESULT_QUEUE = {
    .head = 0,
    .tail = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

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

void initQueues()
{
    if (sem_init(&JOB_QUEUE.items, 0, 0) < 0)
        ERR("sem_init");
    if (sem_init(&JOB_QUEUE.slots, 0, JOB_QUEUE_SIZE) < 0)
        ERR("sem_init");
    if (sem_init(&RESULT_QUEUE.items, 0, 0) < 0)
        ERR("sem_init");
    if (sem_init(&RESULT_QUEUE.slots, 0, RESULT_QUEUE_SIZE) < 0)
        ERR("sem_init");
}

void destroyQueues()
{
    sem_destroy(&JOB_QUEUE.items);
    sem_destroy(&JOB_QUEUE.slots);
    sem_destroy(&RESULT_QUEUE.items);
    sem_destroy(&RESULT_QUEUE.slots);
}

void pushJob(job_t job)
{
    // Producer: wait for a free slot, then put a job into the queue.
    if (TEMP_FAILURE_RETRY(sem_wait(&JOB_QUEUE.slots)) < 0)
        ERR("sem_wait");

    pthread_mutex_lock(&JOB_QUEUE.mutex);
    JOB_QUEUE.jobs[JOB_QUEUE.tail] = job;
    JOB_QUEUE.tail = (JOB_QUEUE.tail + 1) % JOB_QUEUE_SIZE;
    pthread_mutex_unlock(&JOB_QUEUE.mutex);

    // Signal that one more job is ready for a worker.
    sem_post(&JOB_QUEUE.items);
}

void popJob(job_t* job)
{
    // Worker: wait until a job exists, then remove it from the queue.
    if (TEMP_FAILURE_RETRY(sem_wait(&JOB_QUEUE.items)) < 0)
        ERR("sem_wait");

    pthread_mutex_lock(&JOB_QUEUE.mutex);
    *job = JOB_QUEUE.jobs[JOB_QUEUE.head];
    JOB_QUEUE.head = (JOB_QUEUE.head + 1) % JOB_QUEUE_SIZE;
    pthread_mutex_unlock(&JOB_QUEUE.mutex);

    // Signal that one queue slot became free.
    sem_post(&JOB_QUEUE.slots);
}

void pushResult(result_t result)
{
    // Worker: wait for result storage, then publish the finished result.
    if (TEMP_FAILURE_RETRY(sem_wait(&RESULT_QUEUE.slots)) < 0)
        ERR("sem_wait");

    pthread_mutex_lock(&RESULT_QUEUE.mutex);
    RESULT_QUEUE.results[RESULT_QUEUE.tail] = result;
    RESULT_QUEUE.tail = (RESULT_QUEUE.tail + 1) % RESULT_QUEUE_SIZE;
    pthread_mutex_unlock(&RESULT_QUEUE.mutex);

    sem_post(&RESULT_QUEUE.items);
}

int popResult(result_t* result)
{
    // GATHER should not block. If no result is available, just stop draining.
    if (sem_trywait(&RESULT_QUEUE.items) < 0) {
        if (errno == EAGAIN)
            return 0;
        ERR("sem_trywait");
    }

    pthread_mutex_lock(&RESULT_QUEUE.mutex);
    *result = RESULT_QUEUE.results[RESULT_QUEUE.head];
    RESULT_QUEUE.head = (RESULT_QUEUE.head + 1) % RESULT_QUEUE_SIZE;
    pthread_mutex_unlock(&RESULT_QUEUE.mutex);

    sem_post(&RESULT_QUEUE.slots);
    return 1;
}

void* workerLoop(void* data)
{
    int worker_id = *(int*)data;

    for (;;) {
        job_t job;
        popJob(&job);

        if (job.exit)
            break;

        int seed = (int)job.seed;
        result_t result;
        result.worker_id = worker_id;
        result.count = job.count;
        result.seed = job.seed;
        result.value = compute_pi((int)job.count, &seed);

        pushResult(result);
    }

    return NULL;
}

int main(int argc, char** argv)
{
    if (argc != 2)
        usage(argv[0]);

    initQueues();

    pthread_t workers[THREADS];
    int worker_ids[THREADS];

    for (int w = 0; w < THREADS; w++) {
        worker_ids[w] = w;
        if (pthread_create(&workers[w], NULL, workerLoop, &worker_ids[w]) != 0)
            ERR("pthread_create");
    }

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
            // Each pair of uint32_t values becomes one job: count, seed.
            for (int p = 0; p < (count - MIN_SIZE) / 4; p += 2) {
                job_t job;
                job.count = message.params[p];
                job.seed = message.params[p + 1];
                job.exit = 0;
                pushJob(job);
            }
        } else if (strcmp(command, "GATHER") == 0) {
            // Drain all completed results currently available.
            result_t result;
            while (popResult(&result)) {
                printf("result from worker %d: count=%u seed=%u pi=%f\n",
                    result.worker_id, result.count, result.seed, result.value);
            }
        } else if (strcmp(command, "EXIT") == 0) {
            break;
        }
    }

    // One exit job per worker wakes every thread blocked on JOB_QUEUE.items.
    for (int w = 0; w < THREADS; w++) {
        job_t job;
        job.count = 0;
        job.seed = 0;
        job.exit = 1;
        pushJob(job);
    }

    for (int w = 0; w < THREADS; w++)
        if (pthread_join(workers[w], NULL) != 0)
            ERR("pthread_join");

    destroyQueues();
    close(socketfd);
    return 0;
}
