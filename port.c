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
#include <port.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct _ef_port {
    ef_poll_t poll;
    int ptfd;
    int cap;
    port_event_t events[0];
} ef_port_t;

static int ef_port_associate(ef_poll_t *p, int fd, int events, void *ptr, int fired)
{
    ef_port_t *ep = (ef_port_t *)p;
    return port_associate(ep->ptfd, PORT_SOURCE_FD, fd, events, ptr);
}

static int ef_port_dissociate(ef_poll_t *p, int fd, int fired, int onclose)
{
    ef_port_t *ep;

    /*
     * solaris event port will auto dissociate fd after event fired
     */
    if (fired) {
        return 0;
    }

    ep = (ef_port_t *)p;
    return port_dissociate(ep->ptfd, PORT_SOURCE_FD, fd);
}

static int ef_port_unset(ef_poll_t *p, int fd, int events)
{
    return 0;
}

static int ef_port_wait(ef_poll_t *p, ef_event_t *evts, int count, int millisecs)
{
    uint_t nget, idx;
    timespec_t timeout;
    ef_port_t *ep = (ef_port_t *)p;

    if (count > ep->cap) {
        count = ep->cap;
    }

    timeout.tv_sec = millisecs / 1000;
    timeout.tv_nsec = (millisecs % 1000) * 1000000;

    /*
     * at least one event
     */
    nget = 1;

    if (port_getn(ep->ptfd, &ep->events[0], count, &nget, &timeout) < 0) {
        if (errno != ETIME) {
            return -1;
        }
        return 0;
    }

    for (idx = 0; idx < nget; ++idx) {
        evts[idx].events = ep->events[idx].portev_events;
        evts[idx].ptr = ep->events[idx].portev_user;
    }
    return (int)nget;
}

static int ef_port_free(ef_poll_t *p)
{
    ef_port_t *ep = (ef_port_t *)p;
    close(ep->ptfd);
    free(ep);
    return 0;
}

static ef_poll_t *ef_port_create(int cap)
{
    ef_port_t *ep;
    size_t size = sizeof(ef_port_t);

    /*
     * event buffer at least 128
     */
    if (cap < 128) {
        cap = 128;
    }

    size += sizeof(port_event_t) * cap;
    ep = (ef_port_t *)malloc(size);
    if (!ep) {
        return NULL;
    }

    ep->ptfd = port_create();
    if (ep->ptfd < 0) {
        free(ep);
        return NULL;
    }

    ep->poll.associate = ef_port_associate;
    ep->poll.dissociate = ef_port_dissociate;
    ep->poll.unset = ef_port_unset;
    ep->poll.wait = ef_port_wait;
    ep->poll.free = ef_port_free;
    ep->cap = cap;
    return &ep->poll;
}

create_func_t ef_create_poll = ef_port_create;
