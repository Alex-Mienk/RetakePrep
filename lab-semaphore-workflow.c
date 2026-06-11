// Basic semaphore workflow: producer/consumer with pthreads.
//
// Build on Linux:
//   cc -std=c17 -Wall -Wextra -pthread lab-semaphore-workflow.c -o lab-semaphore-workflow
//
// Key idea:
//   sem_empty counts free slots.
//   sem_full counts filled slots.
//   mutex protects the shared buffer index/data.

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUFFER_SIZE 4
#define ITEMS_TO_SEND 10

static int buffer[BUFFER_SIZE];
static int in_pos = 0;
static int out_pos = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t sem_empty;
static sem_t sem_full;

static void die(const char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void* producer(void* arg)
{
    (void)arg;

    for (int value = 1; value <= ITEMS_TO_SEND; value++) {
        // Wait until there is at least one free slot.
        if (sem_wait(&sem_empty) < 0)
            die("sem_wait empty");

        pthread_mutex_lock(&mutex);
        buffer[in_pos] = value;
        printf("producer: wrote %d at slot %d\n", value, in_pos);
        in_pos = (in_pos + 1) % BUFFER_SIZE;
        pthread_mutex_unlock(&mutex);

        // Signal that one filled slot is available.
        sem_post(&sem_full);
        usleep(100000);
    }

    return NULL;
}

static void* consumer(void* arg)
{
    (void)arg;

    for (int i = 0; i < ITEMS_TO_SEND; i++) {
        // Wait until there is at least one filled slot.
        if (sem_wait(&sem_full) < 0)
            die("sem_wait full");

        pthread_mutex_lock(&mutex);
        int value = buffer[out_pos];
        printf("consumer: read %d from slot %d\n", value, out_pos);
        out_pos = (out_pos + 1) % BUFFER_SIZE;
        pthread_mutex_unlock(&mutex);

        // Signal that one free slot is available.
        sem_post(&sem_empty);
        usleep(200000);
    }

    return NULL;
}

int main(void)
{
    pthread_t prod;
    pthread_t cons;

    // pshared = 0 means the semaphore is shared between threads in this process.
    if (sem_init(&sem_empty, 0, BUFFER_SIZE) < 0)
        die("sem_init empty");
    if (sem_init(&sem_full, 0, 0) < 0)
        die("sem_init full");

    if (pthread_create(&prod, NULL, producer, NULL) != 0)
        die("pthread_create producer");
    if (pthread_create(&cons, NULL, consumer, NULL) != 0)
        die("pthread_create consumer");

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    sem_destroy(&sem_empty);
    sem_destroy(&sem_full);
    pthread_mutex_destroy(&mutex);

    return 0;
}
