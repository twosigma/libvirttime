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

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <err.h>

#define LIB_EXPORT __attribute__((visibility("default")))
#define LIB_MAIN __attribute__((constructor))

#define LIBNAME "libvirttime"

#define warnx(fmt, ...)        warnx(LIBNAME": " fmt, ##__VA_ARGS__)
#define errx(status, fmt, ...) errx(status, LIBNAME": " fmt, ##__VA_ARGS__)
#define warn(fmt, ...)         warn(LIBNAME": " fmt, ##__VA_ARGS__)
#define err(status, fmt, ...)  err(status, LIBNAME": " fmt, ##__VA_ARGS__)

#define NSEC_IN_SEC 1000000000

static inline void timespec_add(struct timespec *sum,
                                const struct timespec *left,
                                const struct timespec *right)
{
    sum->tv_sec = left->tv_sec + right->tv_sec;
    sum->tv_nsec = left->tv_nsec + right->tv_nsec;

    if (sum->tv_nsec >= NSEC_IN_SEC) {
        ++sum->tv_sec;
        sum->tv_nsec -= NSEC_IN_SEC;
    }
}

static inline void timespec_sub(struct timespec *diff,
                                const struct timespec *left,
                                const struct timespec *right)
{
    diff->tv_sec = left->tv_sec - right->tv_sec;
    diff->tv_nsec = left->tv_nsec - right->tv_nsec;

    if (diff->tv_nsec < 0) {
        --diff->tv_sec;
        diff->tv_nsec += NSEC_IN_SEC;
    }
}

bool is_pthread_cond_clock_monotonic(pthread_cond_t *cond);
void init_util(void);

extern pid_t gettid(void);

#endif
