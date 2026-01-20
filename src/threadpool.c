#include "custom.h" // DEBUG_PUTS, DEBUG_PRINTF
#include "threadpool.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// In case if "custom.h" isn't #included
#ifndef DEBUG_PRINTF
#define DEBUG_PRINTF(fmt, ...) ((void)0)
#endif
#ifndef DEBUG_PUTS
#define DEBUG_PUTS(msg) ((void)0)
#endif

struct Task {
  void *arg;
  void (*func)(void *);
  Task *next;
};

struct ThreadPool {
  Task *head;                   // Head of tasks' queue
  Task *tail;                   // Tail of tasks' queue
  size_t tasks_count;           // (queue_size) usefull for debug
  size_t alive_threads_count;   // Check are there threads before shutdown
  size_t working_threads_count; // Wait working threads
  pthread_mutex_t mutex;
  pthread_cond_t cond_task_available; // Signal about task been added in queue
  pthread_cond_t cond_wait;           // Signal thread_wait to stop waiting
  bool shutdown; // Finish threads in threadpool_thread_start
};

ThreadPool *threadpool_init(unsigned nthreads) {
  ThreadPool *pool = calloc(1, sizeof(*pool));
  if (!pool) {
    DEBUG_PRINTF("%s: pool calloc fail %s", __func__, strerror(errno));
    return NULL;
  }

  if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
    DEBUG_PUTS("err: mutex_init");
    goto cleanup_pool;
  }
  if (pthread_cond_init(&pool->cond_task_available, NULL) != 0) {
    DEBUG_PUTS("err: pushed_task cond_init");
    goto cleanup_mutex;
  }
  if (pthread_cond_init(&pool->cond_wait, NULL) != 0) {
    DEBUG_PUTS("err: wait cond_init");
    goto cleanup_cond_pushed_task;
  }

  for (unsigned i = 0; i < nthreads; i++) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, threadpool_thread_run, pool) != 0) {
      DEBUG_PUTS("err: pcreate");
      goto cleanup_shutdown;
    }
    pool->alive_threads_count += 1;

    // NOTE: Maybe no need in err checking
    if (pthread_detach(thread)) {
      DEBUG_PUTS("err: pdetatch");
      goto cleanup_shutdown;
    }
  }

  return pool;

cleanup_shutdown:
  pthread_mutex_lock(&pool->mutex);
  pool->shutdown = true;
  pthread_cond_broadcast(&pool->cond_task_available);
  while (pool->alive_threads_count > 0) { // Waiting threads to finish
    pthread_cond_wait(&pool->cond_wait, &pool->mutex);
  }
  pthread_mutex_unlock(&pool->mutex);

  // cleanup_cond_wait:
  if (pthread_cond_destroy(&pool->cond_wait) != 0) {
    DEBUG_PUTS("err: wait cond_destroy");
  }
cleanup_cond_pushed_task:
  if (pthread_cond_destroy(&pool->cond_task_available) != 0) {
    DEBUG_PUTS("err: pushed_task cond_destroy");
  }
cleanup_mutex:
  if (pthread_mutex_destroy(&pool->mutex) != 0) {
    DEBUG_PUTS("err: mutex_destroy");
  }
cleanup_pool:
  free(pool);
  return NULL;
}

// NOTE: waits finishing tasks inside
int threadpool_destroy(ThreadPool *pool) {
  if (pool == NULL) {
    DEBUG_PUTS("threadpool is NULL, nothing to free");
    return -1;
  }
  pthread_mutex_lock(&pool->mutex);
  pool->shutdown = true;
  pthread_cond_broadcast(&pool->cond_task_available);
  pthread_mutex_unlock(&pool->mutex);
  threadpool_wait(pool);

  if (pthread_mutex_destroy(&pool->mutex) != 0) {
    DEBUG_PUTS("err: mutex_destroy");
    goto err;
  }
  if (pthread_cond_destroy(&pool->cond_task_available) != 0) {
    DEBUG_PUTS("err: pushed_task cond_destroy");
    goto err;
  }
  if (pthread_cond_destroy(&pool->cond_wait) != 0) {
    DEBUG_PUTS("err: pushed_task cond_wait");
    goto err;
  }
  free(pool);
  return 0;

err:
  // NOTE: no free for pool
  return -1;
}

// NOTE: may add check is queue is full (add queue_max_size and check it)
int threadpool_push(ThreadPool *pool, void (*func)(void *), void *arg) {
  if (pool == NULL) {
    DEBUG_PUTS("threadpool is NULL, cannot push");
    return -1;
  }

  Task *new_node = malloc(sizeof(*new_node));
  if (!new_node) {
    DEBUG_PRINTF("%s: new_node malloc fail %s", __func__, strerror(errno));
    return -1;
  }
  new_node->next = NULL;
  new_node->func = func;
  new_node->arg = arg;

  pthread_mutex_lock(&pool->mutex); // or move lock at the begining
  pool->tasks_count++;
  if (pool->head == NULL) { // empty queue
    pool->head = new_node;
    pool->tail = new_node;
  } else {
    pool->tail->next = new_node; // not empty queue
    pool->tail = new_node;
  }
  DEBUG_PRINTF("Task pushed. tasks_count: %zu, working_threads_count: %zu",
               pool->tasks_count, pool->working_threads_count);
  pthread_cond_signal(&pool->cond_task_available);
  pthread_mutex_unlock(&pool->mutex);
  return 0;
}

// NOTE: lock mutex before access
int threadpool_pop(ThreadPool *pool, Task **task_ptr) {
  if (pool == NULL) {
    DEBUG_PUTS("pool is NULL, nothing to pop");
    return -1;
  }

// Only for debug
#ifndef NDEBUG
  if (pool->head == NULL) {
    DEBUG_PUTS("threadpool's head is NULL, nothing to pop");
    exit(1);
  }
#endif

  Task *pop_task = pool->head;
  *task_ptr = pop_task;
  pool->head = pop_task->next;

  if (pool->head == NULL) {
    pool->tail = NULL;
  }
  pool->tasks_count--;

  return 0;
}

void *threadpool_thread_run(void *arg) {
  ThreadPool *pool = arg;
  while (true) {
    pthread_mutex_lock(&pool->mutex);
    while (pool->head == NULL && !pool->shutdown) {
      pthread_cond_wait(&pool->cond_task_available, &pool->mutex);
    }
    if (pool->shutdown && pool->head == NULL) {
      pool->alive_threads_count--; // `threadpool_wait()` waits for 0
                                   // alive_threads_count
      if (pool->alive_threads_count == 0) {
        pthread_cond_broadcast(&pool->cond_wait);
      }
      pthread_mutex_unlock(&pool->mutex);
      break;
    }

    // Get task
    Task *task;
    threadpool_pop(pool, &task);
    pool->working_threads_count++;
    DEBUG_PRINTF("Starting Task. tasks_count: %zu, working_threads_count: %zu",
                 pool->tasks_count, pool->working_threads_count);
    pthread_mutex_unlock(&pool->mutex);

    // Execute task
    task->func(task->arg);
    free(task);

    pthread_mutex_lock(&pool->mutex);
    pool->working_threads_count--;

    // Signal for threadpool_wait to stop waiting
    if (pool->working_threads_count == 0) {
      pthread_cond_broadcast(&pool->cond_wait);
    }
    pthread_mutex_unlock(&pool->mutex);
  }

  return (void *)0;
}

// NOTE: may implement wait_idle which do not uses pool->shutdown, but waits
// untill all tasks are finishes
void threadpool_wait(ThreadPool *pool) {
  if (pool == NULL) {
    DEBUG_PUTS("pool is NULL, can't wait");
    return;
  }

  pthread_mutex_lock(&pool->mutex);
  while ((!pool->shutdown && pool->working_threads_count != 0) ||
         (pool->shutdown && pool->alive_threads_count != 0)) {
    pthread_cond_wait(&pool->cond_wait, &pool->mutex);
  }
  pthread_mutex_unlock(&pool->mutex);
}
