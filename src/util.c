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

#include <unistd.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <errno.h>
#include "util.h"

static int pthread_cond_clock_bit_position;

/* With glibc, pthread cond can only use CLOCK_REALTIME or CLOCK_MONOTONIC */

bool is_pthread_cond_clock_monotonic(pthread_cond_t *cond)
{
    const char *data = (void *)cond;
    return !!(data[pthread_cond_clock_bit_position/8] & (1 << (pthread_cond_clock_bit_position % 8)));
}

static void init_pthread_cond_clock_bit_position(void)
{
    /*
     * In the current implementation of glibc, when setting the
     * pthread_cond to use the CLOCK_MONOTONIC source clock,
     * it sets a single bit in the whole struct. The following
     * detects which bit is set.
     */

    union {
        pthread_cond_t cond;
        char data[0];
    } cond;
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&cond.cond, &attr);

    for (size_t i = 0; i < sizeof(cond); i++) {
        if (cond.data[i]) {
            if (pthread_cond_clock_bit_position)
                errx(1, "Found multiple bits for CLOCK_MONOTONIC in pthread_cond");
            pthread_cond_clock_bit_position = i*8 + (__builtin_ffs(cond.data[i]) - 1);
        }
    }

    /* Sanity checks */

    if (!is_pthread_cond_clock_monotonic(&cond.cond))
        errx(1, "is_pthread_cond_clock_monotonic() not working as expected");

    pthread_cond_destroy(&cond.cond);
    pthread_cond_init(&cond.cond, NULL);
    if (is_pthread_cond_clock_monotonic(&cond.cond))
        errx(1, "is_pthread_cond_clock_monotonic() not working as expected");

    pthread_cond_destroy(&cond.cond);
}

static pthread_key_t key_tid;

void init_util(void)
{
    init_pthread_cond_clock_bit_position();

    if ((errno = pthread_key_create(&key_tid, NULL)))
        err(1, "Can't create thread key_tid");
}

pid_t gettid(void)
{
    pid_t tid = (long)pthread_getspecific(key_tid);

    if (tid == 0) {
        tid = syscall(__NR_gettid);
        pthread_setspecific(key_tid, (void *)(long)tid);
    }

    return tid;
}
