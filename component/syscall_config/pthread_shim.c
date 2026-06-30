/*
 * pthread_shim.c — the slice of POSIX threads iperf3 (3.16+, threaded model) needs,
 * mapped onto FreeRTOS tasks + mutex semaphores. Part of component/syscall_config.
 *
 * newlib declares the pthread_* prototypes and types (pthread_t / pthread_mutex_t are
 * pointer-sized integers) but ships no implementation on bare metal — these are the
 * strong defs. A thread is one FreeRTOS task running the start routine; pthread_t holds
 * a small control block; pthread_mutex_t holds a FreeRTOS mutex handle.
 *
 * Caveats (good enough for iperf's "one worker task per stream, cancel+join at test end",
 * not a general pthread): pthread_cancel is vTaskDelete (no cleanup handlers); cancelling
 * a thread that already finished is unsafe; cancel state/type and sigmask are no-ops.
 */
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define IPERF_THR_STACK_WORDS 4096   /* 16 KB: socket send/recv worker (FreeRTOS+TCP send path) */

typedef struct {
    TaskHandle_t      task;
    SemaphoreHandle_t done;
    void           *(*fn)(void *);
    void             *arg;
    void             *ret;
} pth_t;

static void pth_trampoline(void *p)
{
    pth_t *t = (pth_t *)p;
    t->ret = t->fn(t->arg);
    xSemaphoreGive(t->done);
    vTaskDelete(NULL);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    pth_t *t;
    (void)attr;

    t = (pth_t *)malloc(sizeof(*t));
    if (t == NULL) { return EAGAIN; }
    t->fn = start_routine;
    t->arg = arg;
    t->ret = NULL;
    t->done = xSemaphoreCreateBinary();
    if (t->done == NULL) { free(t); return EAGAIN; }

    if (xTaskCreate(pth_trampoline, "iperf_thr", IPERF_THR_STACK_WORDS, t,
                    tskIDLE_PRIORITY + 1, &t->task) != pdPASS) {
        vSemaphoreDelete(t->done);
        free(t);
        return EAGAIN;
    }
    *thread = (pthread_t)(uintptr_t)t;
    return 0;
}

int pthread_join(pthread_t thread, void **retval)
{
    pth_t *t = (pth_t *)(uintptr_t)thread;

    if (t == NULL) { return ESRCH; }
    xSemaphoreTake(t->done, portMAX_DELAY);   /* wait for the trampoline (or cancel) */
    if (retval != NULL) { *retval = t->ret; }
    vSemaphoreDelete(t->done);
    free(t);
    return 0;
}

int pthread_cancel(pthread_t thread)
{
    pth_t *t = (pth_t *)(uintptr_t)thread;

    if (t == NULL) { return ESRCH; }
    if (t->task != NULL) {
        TaskHandle_t task = t->task;
        t->task = NULL;
        vTaskDelete(task);            /* hard kill; the worker is in send/recv at test end */
        xSemaphoreGive(t->done);      /* unblock the matching pthread_join */
    }
    return 0;
}

/* ----------------------------------------------------------------- mutexes --- */

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr)
{
    SemaphoreHandle_t s;
    (void)attr;
    s = xSemaphoreCreateMutex();
    if (s == NULL) { return ENOMEM; }
    *m = (pthread_mutex_t)(uintptr_t)s;
    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *m)
{
    if (m != NULL && *m != 0) { vSemaphoreDelete((SemaphoreHandle_t)(uintptr_t)*m); *m = 0; }
    return 0;
}
int pthread_mutex_lock(pthread_mutex_t *m)
{
    if (m == NULL || *m == 0) { return EINVAL; }
    xSemaphoreTake((SemaphoreHandle_t)(uintptr_t)*m, portMAX_DELAY);
    return 0;
}
int pthread_mutex_unlock(pthread_mutex_t *m)
{
    if (m == NULL || *m == 0) { return EINVAL; }
    xSemaphoreGive((SemaphoreHandle_t)(uintptr_t)*m);
    return 0;
}

/* --------------------------------------------------- attrs / cancel / sig (no-ops) --- */

int pthread_attr_init(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type) { (void)a; (void)type; return 0; }
int pthread_setcancelstate(int state, int *oldstate) { (void)state; if (oldstate) *oldstate = 0; return 0; }
int pthread_setcanceltype(int type, int *oldtype) { (void)type; if (oldtype) *oldtype = 0; return 0; }
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) { (void)how; (void)set; (void)oldset; return 0; }
