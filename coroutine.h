// Copyright (c) 2018-2020 The EFramework Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef _COROUTINE_HEADER_
#define _COROUTINE_HEADER_

#include <unistd.h>
#include <sys/time.h>
#include "fiber.h"
#include "util/list.h"

#define ERROR_CO_EXITED ERROR_FIBER_EXITED
#define ERROR_CO_NOT_INITED ERROR_FIBER_NOT_INITED

typedef struct _ef_coroutine {

    /*
     * nested fiber header struct
     */
    ef_fiber_t fiber;

    /*
     * chain the coroutine to the pool
     */
    ef_list_entry_t full_entry;

    /*
     * after the coroutine exited, chain it to the pool's free list
     */
    ef_list_entry_t free_entry;

    /*
     * coroutine last run time, checked when doing pool shrink
     */
    struct timeval last_run_time;

    /*
     * run count of the coroutine
     */
    unsigned int run_count;
} ef_coroutine_t;

typedef struct _ef_coroutine_pool {

    /*
     * nested fiber sched struct
     */
    ef_fiber_sched_t fiber_sched;

    /*
     * the stack size of the coroutines created in current pool
     */
    size_t stack_size;

    /*
     * the minimum number of coroutines kept when doing pool shrink
     */
    int limit_min;

    /*
     * the maximum number of coroutines this pool can hold
     */
    int limit_max;

    /*
     * chain all coroutines in this pool together
     */
    ef_list_entry_t full_list;

    /*
     * the chain of exited coroutines, can be reused
     */
    ef_list_entry_t free_list;

    /*
     * the number of coroutines in this pool
     */
    int full_count;

    /*
     * the number of exited coroutines in this pool
     */
    int free_count;

    /*
     * total run count of coroutines in the pool
     */
    unsigned long run_count;
} ef_coroutine_pool_t;

typedef ef_fiber_proc_t ef_coroutine_proc_t;

/*
 * init the pool with stack_size, the min/max number of coroutines
 */
int ef_coroutine_pool_init(ef_coroutine_pool_t *pool, size_t stack_size, int limit_min, int limit_max);

/*
 * create a coroutine in the pool and init it, may take one from free_list
 */
ef_coroutine_t *ef_coroutine_create(ef_coroutine_pool_t *pool, size_t header_size, ef_coroutine_proc_t fiber_proc, void *param);

/*
 * resume or first run the coroutine in the pool
 */
long ef_coroutine_resume(ef_coroutine_pool_t *pool, ef_coroutine_t *co, long to_yield);

/*
 * shrink the pool, free(delete) at most max_count coroutines whose idle time exceed idle_millisecs
 */
int ef_coroutine_pool_shrink(ef_coroutine_pool_t *pool, int idle_millisecs, int max_count);

/*
 * get the current "running" coroutine use pool
 */
inline ef_coroutine_t *ef_coroutine_current(ef_coroutine_pool_t *pool) __attribute__((always_inline));

inline ef_coroutine_t *ef_coroutine_current(ef_coroutine_pool_t *pool)
{
    ef_fiber_sched_t *rt = &pool->fiber_sched;
    if (rt->current_fiber == &rt->thread_fiber) {
        return NULL;
    }
    return (ef_coroutine_t*)rt->current_fiber;
}

#endif
