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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

typedef struct kevent kevent_t;

typedef struct _ef_kqueue_item {
    char read;
    char write;
} ef_kqueue_item_t;

typedef struct _ef_kqueue {
    ef_poll_t poll;
    int kqfd;
    int cap;
    ef_kqueue_item_t *items;
    kevent_t *events;
} ef_kqueue_t;

static int ef_kqueue_expand(ef_kqueue_t *ep, int fd)
{
    int cap;
    ef_kqueue_item_t *items;
    kevent_t *events;

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

    items = (ef_kqueue_item_t *)realloc(ep->items, sizeof(ef_kqueue_item_t) * cap);
    if (!items) {
        return -1;
    }

    /*
     * set unused area to 0
     */
    memset(items + ep->cap, 0, sizeof(ef_kqueue_item_t) * (cap - ep->cap));
    ep->items = items;

    events = (kevent_t *)realloc(ep->events, sizeof(kevent_t) * cap);
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

static int ef_kqueue_associate(ef_poll_t *p, int fd, int events, void *ptr, int fired)
{
    ef_kqueue_t *ep;
    kevent_t *e;
    ef_kqueue_item_t item;
    int nevents;

    /*
     * kqueue will not auto dissociate fd after event fired
     */
    if (fired) {
        return 0;
    }

    ep = (ef_kqueue_t *)p;

    if (fd >= ep->cap && ef_kqueue_expand(ep, fd) < 0) {
        return -1;
    }

    item = ep->items[fd];
    e = &ep->events[0];
    nevents = 0;

    if (events & EF_POLLIN) {
        EV_SET(e, fd, EVFILT_READ, EV_ADD, 0, 0, ptr);
        item.read = 1;
        e++;
        nevents++;
    }
    if (events & EF_POLLOUT) {
    	EV_SET(e, fd, EVFILT_WRITE, EV_ADD, 0, 0, ptr);
        item.write = 1;
        nevents++;
    }

    if (kevent(ep->kqfd, &ep->events[0], nevents, NULL, 0, NULL) < 0) {
        return -1;
    }

    ep->items[fd] = item;

    return 0;
}

static int ef_kqueue_dissociate(ef_poll_t *p, int fd, int fired, int onclose)
{
    ef_kqueue_t *ep;
    kevent_t *e;
    int nevents;
    ef_kqueue_item_t item, zero = {0};

    ep = (ef_kqueue_t *)p;

    if (fd >= ep->cap) {
        return 0;
    }

    if (onclose) {
        ep->items[fd] = zero;
        return 0;
    }

    item = ep->items[fd];
    e = &ep->events[0];
    nevents = 0;

    if (item.read) {
        EV_SET(e, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        e++;
        nevents++;
    }
    if (item.write) {
        EV_SET(e, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        nevents++;
    }

    if (kevent(ep->kqfd, &ep->events[0], nevents, NULL, 0, NULL) < 0) {
        return -1;
    }

    ep->items[fd] = zero;

    return 0;
}

static int ef_kqueue_unset(ef_poll_t *p, int fd, int events)
{
    return 0;
}

static int ef_kqueue_wait(ef_poll_t *p, ef_event_t *evts, int count, int millisecs)
{
    int ret, idx;
    struct timespec timeout;
    ef_kqueue_t *ep = (ef_kqueue_t *)p;
    kevent_t *e;
    int events;

    if (count > ep->cap) {
        count = ep->cap;
    }

    timeout.tv_sec = millisecs / 1000;
    timeout.tv_nsec = (millisecs % 1000) * 1000000;

    ret = kevent(ep->kqfd, NULL, 0, ep->events, count, &timeout);
    if (ret <= 0) {
        return ret;
    }

    for (idx = 0; idx < ret; ++idx) {
        e = &ep->events[idx];
        if (e->filter == EVFILT_READ) {
            events = EF_POLLIN;
        } else if (e->filter == EVFILT_WRITE) {
            events = EF_POLLOUT;
        } else {
            events = 0;
        }
        if (e->flags & EV_ERROR) {
            events |= EF_POLLERR;
        }
        if (e->flags & EV_EOF) {
            events |= EF_POLLHUP;
        }
        evts[idx].events = events;
        evts[idx].ptr = e->udata;
    }
    return ret;
}

static int ef_kqueue_free(ef_poll_t *p)
{
    ef_kqueue_t *ep = (ef_kqueue_t *)p;
    close(ep->kqfd);
    free(ep->events);
    free(ep->items);
    free(ep);
    return 0;
}

static ef_poll_t *ef_kqueue_create(int cap)
{
    ef_kqueue_t *ep;
    size_t size;

    /*
     * event buffer at least 128
     */
    if (cap < 128) {
        cap = 128;
    }

    ep = (ef_kqueue_t *)calloc(1, sizeof(ef_kqueue_t));
    if (!ep) {
        return NULL;
    }

    size = sizeof(ef_kqueue_item_t) * cap;
    ep->items = (ef_kqueue_item_t *)malloc(size);
    if (!ep->items) {
        goto error_exit;
    }
    memset(ep->items, 0, size);

    ep->events = (kevent_t *)malloc(sizeof(kevent_t) * cap);
    if (!ep->events) {
        goto error_exit;
    }

    ep->kqfd = kqueue();
    if (ep->kqfd < 0) {
        goto error_exit;
    }

    ep->poll.associate = ef_kqueue_associate;
    ep->poll.dissociate = ef_kqueue_dissociate;
    ep->poll.unset = ef_kqueue_unset;
    ep->poll.wait = ef_kqueue_wait;
    ep->poll.free = ef_kqueue_free;
    ep->cap = cap;
    return &ep->poll;

error_exit:

    if (ep->items) {
        free(ep->items);
    }
    if (ep->events) {
        free(ep->events);
    }
    free(ep);

    return NULL;
}

create_func_t ef_create_poll = ef_kqueue_create;
