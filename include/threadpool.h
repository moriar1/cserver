#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stdbool.h>

typedef struct Task Task;
typedef struct ThreadPool ThreadPool;

ThreadPool *threadpool_init(unsigned);
int threadpool_destroy(ThreadPool *);
void *threadpool_thread_run(void *);
int threadpool_push(ThreadPool *, void (*)(void *), void *);
int threadpool_pop(ThreadPool *, Task **);
void threadpool_wait(ThreadPool *);

#endif // THREADPOOL_H
