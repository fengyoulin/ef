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

#ifndef _FIBER_HEADER_
#define _FIBER_HEADER_

#include <sys/mman.h>

#define ERROR_FIBER_EXITED     (-1)
#define ERROR_FIBER_NOT_INITED (-2)

#define FIBER_STATUS_EXITED 0
#define FIBER_STATUS_INITED 1

typedef struct _ef_fiber ef_fiber_t;
typedef struct _ef_fiber_sched ef_fiber_sched_t;

/*
     the fiber layout

    |----------------|
    | ef_fiber_t and |
    | other headers  |
    |----------------| <- stack_upper
    |                |
    |                |
    |                |
    |                |
    |                |
    | really mapped  |
    |                |
    |                |
    |                |
    |                | <- stack_ptr, used by user space context switch
    |                |
    |----------------| <- stack_lower
    |                |
    ~                ~
    ~                ~
    |                |
    | reserved area  |
    |                |
    |                |
    |                |
    |----------------|
    | one page guard |
    |----------------| <- stack_area

*/
struct _ef_fiber {

    /*
     * total memory area size
     */
    size_t stack_size;

    /*
     * start address of reserved area
     */
    void *stack_area;

    /*
     * fiber stack start from here
     */
    void *stack_upper;

    /*
     * lower boundary of really mapped memory area
     */
    void *stack_lower;

    /*
     * save or restore stack pointer on context switching
     */
    void *stack_ptr;

    /*
     * currently initialized or exited
     */
    long status;

    /*
     * when do yield or on exiting, return to parent
     */
    ef_fiber_t *parent;

    /*
     * find the sched struct for the fiber
     */
    ef_fiber_sched_t *sched;
};

struct _ef_fiber_sched {

    /*
     * current "running" fiber
     */
    ef_fiber_t *current_fiber;

    /*
     * just save the stack_ptr of system thread
     */
    ef_fiber_t thread_fiber;
};

typedef long (*ef_fiber_proc_t)(void *param);

#define ef_fiber_is_exited(fiber) ((fiber)->status == FIBER_STATUS_EXITED)

/*
 * run a just initialized fiber or resume a fiber which doing yield
 * sndval will be the return value of the yield function in the latter
 */
int ef_fiber_resume(ef_fiber_sched_t *rt, ef_fiber_t *to, long sndval, long *retval);

/*
 * yield cpu and return to parent fiber (maybe system thread)
 * the sndval will be the retval of the resume function in parent
 */
long ef_fiber_yield(ef_fiber_sched_t *rt, long sndval);

/*
 * create a fiber with stack_size sized stack, and reserve header_size bytes
 * to hold the header(s), init the fiber with fiber_proc and param
 */
ef_fiber_t *ef_fiber_create(ef_fiber_sched_t *rt, size_t stack_size, size_t header_size, ef_fiber_proc_t fiber_proc, void *param);

/*
 * expand the lower boundary of the fiber stack to addr
 */
int ef_fiber_expand_stack(ef_fiber_t *fiber, void *addr);

/*
 * init the sched rt, maybe you want to handle sigsegv yourself
 */
int ef_fiber_init_sched(ef_fiber_sched_t *rt, int handle_sigsegv);

/*
 * init a fiber with fiber_proc and param
 */
void ef_fiber_init(ef_fiber_t *fiber, ef_fiber_proc_t fiber_proc, void *param);

/*
 * delete a fiber always destroy its whole memory area
 */
void ef_fiber_delete(ef_fiber_t *fiber);

#endif
