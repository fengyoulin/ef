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

#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include "fiber.h"

static long ef_page_size = 0;
static ef_fiber_sched_t *ef_fiber_sched = NULL;

long ef_fiber_internal_swap(void *new_sp, void **old_sp_ptr, long retval);

void *ef_fiber_internal_init(ef_fiber_t *fiber, ef_fiber_proc_t fiber_proc, void *param);

ef_fiber_t *ef_fiber_create(ef_fiber_sched_t *rt, size_t stack_size, size_t header_size, ef_fiber_proc_t fiber_proc, void *param)
{
    ef_fiber_t *fiber;
    void *stack;
    long page_size = ef_page_size;

    if (stack_size == 0) {
        stack_size = (size_t)page_size;
    }

    /*
     * make the stack_size an integer multiple of page_size
     */
    stack_size = (size_t)((stack_size + page_size - 1) & ~(page_size - 1));

    /*
     * reserve the stack area, no physical pages here
     */
    stack = mmap(NULL, stack_size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (MAP_FAILED == stack) {
        return NULL;
    }

    /*
     * map the highest page in the stack area
     */
    if (mprotect((char *)stack + stack_size - page_size, page_size, PROT_READ | PROT_WRITE) < 0) {
        munmap(stack, stack_size);
        return NULL;
    }

    /*
     * the topmost header_size bytes used by ef_fiber_t and
     * maybe some outter struct which ef_fiber_t nested in
     */
    fiber = (ef_fiber_t*)((char *)stack + stack_size - header_size);
    fiber->stack_size = stack_size;
    fiber->stack_area = stack;
    fiber->stack_upper = (char *)stack + stack_size - header_size;
    fiber->stack_lower = (char *)stack + stack_size - page_size;
    fiber->sched = rt;
    ef_fiber_init(fiber, fiber_proc, param);
    return fiber;
}

void ef_fiber_init(ef_fiber_t *fiber, ef_fiber_proc_t fiber_proc, void *param)
{
    fiber->stack_ptr = ef_fiber_internal_init(fiber, fiber_proc, (param != NULL) ? param : fiber);
}

void ef_fiber_delete(ef_fiber_t *fiber)
{
    /*
     * free the stack area, contains the ef_fiber_t
     * of course the fiber cannot delete itself
     */
    munmap(fiber->stack_area, fiber->stack_size);
}

int ef_fiber_resume(ef_fiber_sched_t *rt, ef_fiber_t *to, long sndval, long *retval)
{
    long ret;
    ef_fiber_t *current;

    /*
     * ensure the fiber is initialized and not exited
     */
    if (to->status != FIBER_STATUS_INITED) {
        if (to->status == FIBER_STATUS_EXITED) {
            return ERROR_FIBER_EXITED;
        }
        return ERROR_FIBER_NOT_INITED;
    }

    current = rt->current_fiber;
    to->parent = current;
    rt->current_fiber = to;
    ret = ef_fiber_internal_swap(to->stack_ptr, &current->stack_ptr, sndval);

    if (retval) {
        *retval = ret;
    }
    return 0;
}

long ef_fiber_yield(ef_fiber_sched_t *rt, long sndval)
{
    ef_fiber_t *current = rt->current_fiber;
    rt->current_fiber = current->parent;
    return ef_fiber_internal_swap(current->parent->stack_ptr, &current->stack_ptr, sndval);
}

int ef_fiber_expand_stack(ef_fiber_t *fiber, void *addr)
{
    int retval = -1;

    /*
     * align to the nearest lower page boundary
     */
    char *lower = (char *)((long)addr & ~(ef_page_size - 1));

    /*
     * the last one page keep unmaped for guard
     */
    if (lower - (char *)fiber->stack_area >= ef_page_size &&
        lower < (char *)fiber->stack_lower) {
        size_t size = (char *)fiber->stack_lower - lower;
        retval = mprotect(lower, size, PROT_READ | PROT_WRITE);
        if (retval >= 0) {
            fiber->stack_lower = lower;
        }
    }
    return retval;
}

void ef_fiber_sigsegv_handler(int sig, siginfo_t *info, void *ucontext)
{
    /*
     * we need core dump if failed to expand fiber stack
     */
    if ((SIGSEGV != sig && SIGBUS != sig) ||
        ef_fiber_expand_stack(ef_fiber_sched->current_fiber, info->si_addr) < 0) {
        raise(SIGABRT);
    }
}

int ef_fiber_init_sched(ef_fiber_sched_t *rt, int handle_sigsegv)
{
    stack_t ss;
    struct sigaction sa = {0};

    /*
     * the global pointer used by SIGSEGV handler
     */
    ef_fiber_sched = rt;

    rt->current_fiber = &rt->thread_fiber;
    ef_page_size = sysconf(_SC_PAGESIZE);
    if (ef_page_size < 0) {
        return -1;
    }

    if (!handle_sigsegv) {
        return 0;
    }

    /*
     * use alt stack, when SIGSEGV caused by fiber stack, user stack maybe invalid
     */
    ss.ss_sp = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ss.ss_sp == NULL) {
        return -1;
    }
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1) {
        return -1;
    }

    /*
     * register SIGSEGV handler for fiber stack expanding
     */
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = ef_fiber_sigsegv_handler;
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        return -1;
    }

    /*
     * maybe SIGBUS on macos
     */
    if (sigaction(SIGBUS, &sa, NULL) < 0) {
        return -1;
    }
    return 0;
}
