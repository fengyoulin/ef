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

#include "coroutine.h"
#include "util/util.h"

int ef_coroutine_pool_init(ef_coroutine_pool_t *pool, size_t stack_size, int limit_min, int limit_max)
{
    if (ef_fiber_init_sched(&pool->fiber_sched, 1) < 0) {
        return -1;
    }
    pool->stack_size = stack_size;
    pool->limit_min = limit_min;
    pool->limit_max = limit_max;
    ef_list_init(&pool->full_list);
    ef_list_init(&pool->free_list);
    pool->full_count = 0;
    pool->free_count = 0;
    pool->run_count = 0;
    return 0;
}

ef_coroutine_t *ef_coroutine_create(ef_coroutine_pool_t *pool, size_t header_size, ef_coroutine_proc_t fiber_proc, void *param)
{
    ef_coroutine_t *co;

    /*
     * try take one from the free_list
     */
    if (pool->free_count > 0) {
        --pool->free_count;
        co = CAST_PARENT_PTR(ef_list_remove_after(&pool->free_list), ef_coroutine_t, free_entry);
        ef_fiber_init(&co->fiber, fiber_proc, param);
        return co;
    }

    if (pool->full_count >= pool->limit_max) {
        return NULL;
    }

    /*
     * create use the fiber api
     */
    co = (ef_coroutine_t *)ef_fiber_create(&pool->fiber_sched, pool->stack_size, header_size, fiber_proc, param);
    if (!co) {
        return NULL;
    }

    co->run_count = 0;

    ++pool->full_count;
    ef_list_insert_after(&pool->full_list, &co->full_entry);
    return co;
}

long ef_coroutine_resume(ef_coroutine_pool_t *pool, ef_coroutine_t *co, long to_yield)
{
    long retval = 0;

    int res = ef_fiber_resume(&pool->fiber_sched, &co->fiber, to_yield, &retval);
    if (res < 0) {
        return retval;
    }

    /*
     * add to free_list when exited
     */
    if (ef_fiber_is_exited(&co->fiber)) {
        ++co->run_count;
        gettimeofday(&co->last_run_time, NULL);
        ef_list_insert_after(&pool->free_list, &co->free_entry);
        ++pool->free_count;
        ++pool->run_count;
    }

    return retval;
}

int ef_coroutine_pool_shrink(ef_coroutine_pool_t *pool, int idle_millisecs, int max_count)
{
    int beyond_min, free_count = 0;
    struct timeval tv = {0};
    ef_list_entry_t *list_tail;

    if (pool->free_count <= 0 || (max_count > 0 && pool->full_count <= pool->limit_min)) {
        return 0;
    }

    /*
     * calculate the number to free
     */
    beyond_min = pool->full_count - pool->limit_min;
    if (max_count > beyond_min) {
        max_count = beyond_min;
    }
    if (max_count < 0) {
        max_count = -max_count;
    }

    gettimeofday(&tv, NULL);

    /*
     * free at most max_count fibers from free_list
     */
    list_tail = ef_list_entry_before(&pool->free_list);
    while (list_tail != &pool->free_list && max_count--) {

        ef_coroutine_t *co = CAST_PARENT_PTR(list_tail, ef_coroutine_t, free_entry);
        list_tail = ef_list_entry_before(list_tail);

        if (((tv.tv_sec - co->last_run_time.tv_sec) * 1000 > idle_millisecs) ||
            (((tv.tv_sec - co->last_run_time.tv_sec) * 1000 == idle_millisecs) && tv.tv_usec - co->last_run_time.tv_usec >= idle_millisecs % 1000)) {
            --pool->free_count;
            --pool->full_count;
            ef_list_remove(&co->free_entry);
            ef_list_remove(&co->full_entry);
            ++free_count;
            ef_fiber_delete(&co->fiber);
        } else {
            break;
        }
    }
    return free_count;
}
