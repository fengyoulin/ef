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
#include <unistd.h>
#include <sys/epoll.h>

typedef struct epoll_event epoll_event_t;

typedef struct _ef_epoll {
    ef_poll_t poll;
    int epfd;
    int cap;
    epoll_event_t events[0];
} ef_epoll_t;

static int ef_epoll_associate(ef_poll_t *p, int fd, int events, void *ptr, int fired)
{
    ef_epoll_t *ep;
    epoll_event_t *e;

    /*
     * epoll will not auto dissociate fd after event fired
     */
    if (fired) {
        return 0;
    }

    ep = (ef_epoll_t *)p;
    e = &ep->events[0];
    e->events = events;
    e->data.ptr = ptr;

    return epoll_ctl(ep->epfd, EPOLL_CTL_ADD, fd, e);
}

static int ef_epoll_dissociate(ef_poll_t *p, int fd, int fired, int onclose)
{
    ef_epoll_t *ep = (ef_epoll_t *)p;
    epoll_event_t *e = &ep->events[0];
    return epoll_ctl(ep->epfd, EPOLL_CTL_DEL, fd, e);
}

static int ef_epoll_unset(ef_poll_t *p, int fd, int events)
{
    return 0;
}

static int ef_epoll_wait(ef_poll_t *p, ef_event_t *evts, int count, int millisecs)
{
    int ret, idx;
    ef_epoll_t *ep = (ef_epoll_t *)p;

    if (count > ep->cap) {
        count = ep->cap;
    }

    ret = epoll_wait(ep->epfd, &ep->events[0], count, millisecs);
    if (ret <= 0) {
        return ret;
    }

    for (idx = 0; idx < ret; ++idx) {
        evts[idx].events = ep->events[idx].events;
        evts[idx].ptr = ep->events[idx].data.ptr;
    }
    return ret;
}

static int ef_epoll_free(ef_poll_t *p)
{
    ef_epoll_t *ep = (ef_epoll_t *)p;
    close(ep->epfd);
    free(ep);
    return 0;
}

static ef_poll_t *ef_epoll_create(int cap)
{
    ef_epoll_t *ep;
    size_t size = sizeof(ef_epoll_t);

    /*
     * event buffer at least 128
     */
    if (cap < 128) {
        cap = 128;
    }

    size += sizeof(epoll_event_t) * cap;
    ep = (ef_epoll_t *)malloc(size);
    if (!ep) {
        return NULL;
    }

    ep->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (ep->epfd < 0) {
        free(ep);
        return NULL;
    }

    ep->poll.associate = ef_epoll_associate;
    ep->poll.dissociate = ef_epoll_dissociate;
    ep->poll.unset = ef_epoll_unset;
    ep->poll.wait = ef_epoll_wait;
    ep->poll.free = ef_epoll_free;
    ep->cap = cap;
    return &ep->poll;
}

create_func_t ef_create_poll = ef_epoll_create;
