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

#include "poll.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

typedef struct epoll_event epoll_event_t;

typedef struct _ef_epoll_item {
    int fd;
    int waiting;
    int fired;
    void *ptr;
} ef_epoll_item_t;

typedef struct _ef_epoll {
    ef_poll_t poll;
    int epfd;
    int cap;
    int used;
    int fill;
    int *index;
    ef_epoll_item_t *items;
    epoll_event_t *events;
} ef_epoll_t;

static int ef_epoll_expand(ef_epoll_t *ep, int fd)
{
    int cap;
    int *index;
    ef_epoll_item_t *items;
    epoll_event_t *events;

    cap = ep->cap;
    if (cap > fd) {
        return 0;
    }

    /*
     * every time multiply 2
     */
    while (cap <= fd) {
        cap <<= 1;
    }

    index = (int *)realloc(ep->index, sizeof(int) * cap);
    if (!index) {
        return -1;
    }

    /*
     * set unused area to -1
     */
    memset(index + ep->cap, -1, sizeof(int) * (cap - ep->cap));
    ep->index = index;

    items = (ef_epoll_item_t *)realloc(ep->items, sizeof(ef_epoll_item_t) * cap);
    if (!items) {
        return -1;
    }

    ep->items = items;

    events = (epoll_event_t *)realloc(ep->events, sizeof(epoll_event_t) * cap);
    if (!events) {
        return -1;
    }

    ep->events = events;

    /*
     * update cap to the new
     */
    ep->cap = cap;

    return 0;
}

static int ef_epoll_associate(ef_poll_t *p, int fd, int events, void *ptr, int fired)
{
    ef_epoll_t *ep;
    ef_epoll_item_t *pi, tmp;
    epoll_event_t *e;
    int idx;

    ep = (ef_epoll_t *)p;

    if (fd >= ep->cap && ef_epoll_expand(ep, fd) < 0) {
        return -1;
    }

    idx = ep->index[fd];
    if (idx < 0) {
        idx = ep->used++;
        ep->index[fd] = idx;

        pi = &ep->items[idx];
        pi->fd = fd;
        pi->waiting = events;
        pi->fired = EPOLLOUT;
        pi->ptr = ptr;

        e = &ep->events[0];
        e->events = EPOLLIN | EPOLLOUT | EPOLLET;
        e->data.fd = fd;

        if (epoll_ctl(ep->epfd, EPOLL_CTL_ADD, fd, e) < 0) {
            return -1;
        }
    } else {
        pi = &ep->items[idx];
        pi->waiting = events;
        pi->ptr = ptr;
    }

    /*
     * move to filled area
     */
    if ((pi->waiting | EPOLLERR | EPOLLHUP) & pi->fired) {
        if (idx > ep->fill) {
            ep->index[ep->items[ep->fill].fd] = idx;
            ep->index[pi->fd] = ep->fill;

            tmp = ep->items[ep->fill];
            ep->items[ep->fill] = *pi;
            *pi = tmp;
        }
        if (idx >= ep->fill) {
            ++ep->fill;
        }
        return 1;
    }

    return 0;
}

static int ef_epoll_dissociate(ef_poll_t *p, int fd, int fired, int onclose)
{
    ef_epoll_t *ep;
    ef_epoll_item_t *pi, tmp;
    epoll_event_t *e;
    int idx;

    ep = (ef_epoll_t *)p;

    if (fd >= ep->cap) {
        return 0;
    }

    idx = ep->index[fd];
    if (idx < 0) {
        return 0;
    }

    pi = &ep->items[idx];

    if (onclose) {

        /*
         * set index to -1
         */
        ep->index[fd] = -1;

        /*
         * remove from filled if inside of
         */
        if (idx < ep->fill) {
            --ep->fill;
            if (idx < ep->fill) {
                *pi = ep->items[ep->fill];
                ep->index[pi->fd] = idx;
                idx = ep->fill;
                pi = &ep->items[idx];
            }
        }

        /*
         * remove from used
         */
        --ep->used;
        if (idx < ep->used) {
            *pi = ep->items[ep->used];
            ep->index[pi->fd] = idx;
        }

        /*
         * delete from epoll
         */
        e = &ep->events[0];
        return epoll_ctl(ep->epfd, EPOLL_CTL_DEL, fd, e);
    } else {
        pi->waiting = 0;

        /*
         * remove from filled
         */
        if (idx < ep->fill) {
            --ep->fill;
            if (idx < ep->fill) {
                ep->index[ep->items[ep->fill].fd] = idx;
                ep->index[pi->fd] = ep->fill;

                tmp = ep->items[ep->fill];
                ep->items[ep->fill] = *pi;
                *pi = tmp;
            }
        }
    }

    return 0;
}

static int ef_epoll_unset(ef_poll_t *p, int fd, int events)
{
    ef_epoll_t *ep;
    ef_epoll_item_t *pi, tmp;
    int idx;

    ep = (ef_epoll_t *)p;

    if (fd >= ep->cap) {
        return 0;
    }

    idx = ep->index[fd];
    if (idx < 0) {
        return 0;
    }

    pi = &ep->items[idx];
    pi->fired &= (~events);

    /*
     * remove from filled
     */
    if (idx < ep->fill && !((pi->waiting | EPOLLERR | EPOLLHUP) & pi->fired)) {
        --ep->fill;
        if (idx < ep->fill) {
            ep->index[ep->items[ep->fill].fd] = idx;
            ep->index[pi->fd] = ep->fill;

            tmp = ep->items[ep->fill];
            ep->items[ep->fill] = *pi;
            *pi = tmp;
        }
    }

    return 0;
}

static int ef_epoll_wait(ef_poll_t *p, ef_event_t *evts, int count, int millisecs)
{
    int ret, cur, idx;
    ef_epoll_t *ep;
    ef_epoll_item_t *pi, tmp;

    ep = (ef_epoll_t *)p;

    if (!ep->fill) {
        ret = epoll_wait(ep->epfd, ep->events, ep->cap, millisecs);
        if (ret < 0) {
            return ret;
        }

        for (cur = 0; cur < ret; ++cur) {
            idx = ep->index[ep->events[cur].data.fd];
            pi = &ep->items[idx];
            pi->fired |= ep->events[cur].events;

            /*
             * move to filled area
             */
            if (idx >= ep->fill && ((pi->waiting | EPOLLERR | EPOLLHUP) & pi->fired)) {
                if (idx > ep->fill) {
                    ep->index[ep->items[ep->fill].fd] = idx;
                    ep->index[pi->fd] = ep->fill;

                    tmp = ep->items[ep->fill];
                    ep->items[ep->fill] = *pi;
                    *pi = tmp;
                }
                ++ep->fill;
            }
        }
    }

    if (count > ep->fill) {
        count = ep->fill;
    }

    for (idx = 0; idx < count; ++idx) {
        pi = &ep->items[idx];
        evts[idx].events = ((pi->waiting | EPOLLERR | EPOLLHUP) & pi->fired);
        evts[idx].ptr = pi->ptr;
    }
    return count;
}

static int ef_epoll_free(ef_poll_t *p)
{
    ef_epoll_t *ep = (ef_epoll_t *)p;
    close(ep->epfd);
    free(ep->events);
    free(ep->items);
    free(ep->index);
    free(ep);
    return 0;
}

static ef_poll_t *ef_epoll_create(int cap)
{
    ef_epoll_t *ep;
    size_t size;

    /*
     * event buffer at least 128
     */
    if (cap < 128) {
        cap = 128;
    }

    ep = (ef_epoll_t *)calloc(1, sizeof(ef_epoll_t));
    if (!ep) {
        return NULL;
    }

    size = sizeof(int) * cap;
    ep->index = (int *)malloc(size);
    if (!ep->index) {
        goto error_exit;
    }
    memset(ep->index, -1, size);

    ep->items = (ef_epoll_item_t *)malloc(sizeof(ef_epoll_item_t) * cap);
    if (!ep->items) {
        goto error_exit;
    }

    ep->events = (epoll_event_t *)malloc(sizeof(epoll_event_t) * cap);
    if (!ep->events) {
        goto error_exit;
    }

    ep->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (ep->epfd < 0) {
        goto error_exit;
    }

    ep->poll.associate = ef_epoll_associate;
    ep->poll.dissociate = ef_epoll_dissociate;
    ep->poll.unset = ef_epoll_unset;
    ep->poll.wait = ef_epoll_wait;
    ep->poll.free = ef_epoll_free;
    ep->cap = cap;
    ep->used = 0;
    ep->fill = 0;
    return &ep->poll;

error_exit:

    if (ep->index) {
        free(ep->index);
    }
    if (ep->items) {
        free(ep->items);
    }
    if (ep->events) {
        free(ep->events);
    }
    free(ep);

    return NULL;
}

create_func_t ef_create_poll = ef_epoll_create;
