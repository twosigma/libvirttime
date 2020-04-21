/*
 * Copyright 2019 Two Sigma Investments, LP.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <dlfcn.h>
#include "cpuid.h"
#include "util.h"
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <string.h>

/* TODO sys_futex() when not usign the FUTEX_CLOCK_REALTIME flag. (for FUTEX_WAIT, FUTEX_WAIT_BITSET, FUTEX_WAIT_REQUEUE_PI) */
/* TODO sys_timer_gettime */
/* TODO sys_timer_settime */

/*
 * During migration, calls get restarted, but still with the old time offset.
 * We need the new time offset. So we place our timespec argument on the mmap
 * region. Note that with 32k max pid, we are looking at 0.5Mb of data. Not
 * terrible.
 */
struct per_thread_conf {
    struct timespec ts;
};

static struct virt_time_config {
    struct timespec ts_offset;
    struct per_thread_conf thread_confs[0];
} *conf;
static pid_t max_tid;

static bool init_done;

static struct per_thread_conf *get_current_thread_conf(void)
{
    pid_t tid = gettid();
    if (tid > max_tid)
        errx(1, "Assumed max_tid=%d, but got tid=%d", max_tid, tid);
    return &conf->thread_confs[tid];
}

static bool should_virt_clock(clockid_t clk_id)
{
    switch (clk_id) {
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_BOOTTIME:
        return true;
    default:
        return false;
    }
}

static int (*real_clock_gettime)(clockid_t clk_id, struct timespec *ts);
LIB_EXPORT
int clock_gettime(clockid_t clk_id, struct timespec *ts)
{
    if (should_virt_clock(clk_id)) {
        struct timespec _ts;
        int ret = real_clock_gettime(clk_id, &_ts);
        if (ret == -1)
            return ret;
        timespec_sub(ts, &_ts, &conf->ts_offset);
        return 0;
    } else {
        return real_clock_gettime(clk_id, ts);
    }
}

static int (*real_clock_nanosleep)(clockid_t clock_id, int flags,
                                   const struct timespec *request,
                                   struct timespec *remain);
LIB_EXPORT
int clock_nanosleep(clockid_t clk_id, int flags,
                    const struct timespec *request,
                    struct timespec *remain)
{

    if (should_virt_clock(clk_id) && (flags & TIMER_ABSTIME) && request) {
        /* remain is not used when using TIMER_ABSTIME */
        struct timespec *_request = &get_current_thread_conf()->ts;
        timespec_add(_request, request, &conf->ts_offset);
        return real_clock_nanosleep(clk_id, flags, _request, remain);
    }

    return real_clock_nanosleep(clk_id, flags, request, remain);
}

static int (*real_pthread_cond_timedwait)(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex,
                                          const struct timespec *restrict abstime);
LIB_EXPORT
int pthread_cond_timedwait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex,
                           const struct timespec *restrict abstime)
{
    if (is_pthread_cond_clock_monotonic(cond)) {
        struct timespec *_abstime = &get_current_thread_conf()->ts;
        timespec_add(_abstime, abstime, &conf->ts_offset);
        return real_pthread_cond_timedwait(cond, mutex, _abstime);
    }
    return real_pthread_cond_timedwait(cond, mutex, abstime);
}

/*
 * We don't need to interpose pthread_mutex_timedlock() as there is no API to
 * make them use the CLOCK_MONOTONIC clock.
 */

static void init_conf(void)
{
    char *conf_path = getenv("VIRT_TIME_CONF");
    if (!conf_path)
        errx(1, "VIRT_TIME_CONF must be set");

    int fd = open(conf_path, O_RDWR);
    if (fd == -1)
        err(1, "Can't open config");

    struct stat stat;
    if (fstat(fd, &stat) == -1)
        err(1, "Can't stat file");

    if (stat.st_size < (off_t)sizeof(*conf))
        err(1, "Config file too small");

    conf = mmap(NULL, stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (!conf)
        err(1, "Can't mmap the config");

    max_tid = (stat.st_size - sizeof(*conf)) / sizeof(conf->thread_confs[0]);

    close(fd);
}

/*
 * We can't get the real_dlsym via dlsym, as are overriding it.
 * So we use an internal libc function.
 * Is this a pile of hacks? yes. Does it work? yes.
 */
extern void *_dl_sym (void *handle, const char *name, void *who);

static void *(*real_dlsym)(void *handle, const char *symbol);
LIB_EXPORT
void *dlsym(void *handle, const char *symbol)
{
    /*
     * Note: here we use __builtin_return_address, using dlsym would
     * not work as another library might be hijacking it.
     */
    if (!real_dlsym)
        real_dlsym = _dl_sym(RTLD_NEXT, "dlsym", __builtin_return_address(0));

    /*
     * The JVM gets clock_gettime via dlsym, due to some bug (6348968 in
     * their bug tracking system).
     */
    if (!strcmp(symbol, "clock_gettime") && init_done)
        return clock_gettime;
    return real_dlsym(handle, symbol);
}

LIB_MAIN
static void lib_main(void)
{
    init_conf();

    /*
     * XXX if other library init code call the functions
     * we are interposing on, we'll have problems as we might
     * not yet be initialized.
     */
    real_clock_gettime = dlsym(RTLD_NEXT, "clock_gettime");
    real_clock_nanosleep = dlsym(RTLD_NEXT, "clock_nanosleep");

    /*
     * Can't do RTLD_NEXT on pthread_cond_timedwait, we get the wrong
     * function (something like old_pthread_cond_timedwait).
     */
    void *handle = dlopen("libpthread.so.0", RTLD_NOW);
    if (!handle)
        errx(1, "Cannot load libpthread.so.0");

    real_pthread_cond_timedwait = dlsym(handle, "pthread_cond_timedwait");

    init_util();

    init_done = true;
}
