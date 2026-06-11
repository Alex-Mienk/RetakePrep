// When to initialize attributes.
//
// Attribute objects are initialized BEFORE creating the object that uses them.
// They are only configuration templates. After pthread_create/pthread_mutex_init,
// you can usually destroy the attribute object immediately.
//
// Build:
//   cc -std=c17 -Wall -Wextra -pthread lab-init-attributes.c -o lab-init-attributes

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static void die_pthread(int error, const char* msg)
{
    if (error != 0) {
        fprintf(stderr, "%s failed: %d\n", msg, error);
        exit(EXIT_FAILURE);
    }
}

static void* worker(void* arg)
{
    int id = *(int*)arg;
    printf("worker %d is running\n", id);
    return NULL;
}

int main(void)
{
    pthread_t thread;
    pthread_attr_t thread_attr;
    pthread_mutex_t mutex;
    pthread_mutexattr_t mutex_attr;
    int id = 7;

    // 1. Initialize thread attributes before pthread_create.
    die_pthread(pthread_attr_init(&thread_attr), "pthread_attr_init");

    // Example setting: create a joinable thread.
    // Joinable is the default, but this shows where attributes are configured.
    die_pthread(pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE),
        "pthread_attr_setdetachstate");

    die_pthread(pthread_create(&thread, &thread_attr, worker, &id), "pthread_create");

    // Attribute object is no longer needed after pthread_create returns.
    die_pthread(pthread_attr_destroy(&thread_attr), "pthread_attr_destroy");

    die_pthread(pthread_join(thread, NULL), "pthread_join");

    // 2. Initialize mutex attributes before pthread_mutex_init.
    die_pthread(pthread_mutexattr_init(&mutex_attr), "pthread_mutexattr_init");

    // Example setting: ERRORCHECK mutex reports relocking mistakes by same thread.
    die_pthread(pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK),
        "pthread_mutexattr_settype");

    die_pthread(pthread_mutex_init(&mutex, &mutex_attr), "pthread_mutex_init");

    // Attribute object is no longer needed after pthread_mutex_init returns.
    die_pthread(pthread_mutexattr_destroy(&mutex_attr), "pthread_mutexattr_destroy");

    die_pthread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    printf("main owns the mutex now\n");
    die_pthread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");

    die_pthread(pthread_mutex_destroy(&mutex), "pthread_mutex_destroy");

    return 0;
}
