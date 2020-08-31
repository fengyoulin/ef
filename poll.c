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
#include <poll.h>
#include <stdlib.h>
#include <string.h>

typedef struct pollfd pollfd_t;

typedef struct _ef_poll_index {
    int idx;
    void *ptr;
} ef_poll_index_t;

typedef struct _ef_pollsys {
    ef_poll_t poll;
    int cap;
    int nfds;
    ef_poll_index_t *index;
    pollfd_t *pfds;
} ef_pollsys_t;

static int ef_poll_expand(ef_pollsys_t *ep, int fd)
{
    int cap;
    ef_poll_index_t *index;
    pollfd_t *pfds;

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

    index = (ef_poll_index_t *)realloc(ep->index, sizeof(ef_poll_index_t) * cap);
    if (!index) {
        return -1;
    }

    /*
     * set unused area to -1
     */
    memset(index + ep->cap, -1, sizeof(ef_poll_index_t) * (cap - ep->cap));
    ep->index = index;

    pfds = (pollfd_t *)realloc(ep->pfds, sizeof(pollfd_t) * cap);
    if (!pfds) {
        return -1;
    }

    ep->pfds = pfds;

    /*
     * update cap to the new
     */
    ep->cap = cap;

    return 0;
}

static int ef_poll_associate(ef_poll_t *p, int fd, int events, void *ptr, int fired)
{
    ef_pollsys_t *ep;
    pollfd_t *pf;
    int idx;

    /*
     * of course we will not auto dissociate fd after event fired
     */
    if (fired) {
        return 0;
    }

    ep = (ef_pollsys_t *)p;
    if (ep->cap <= fd && ef_poll_expand(ep, fd) < 0) {
        return -1;
    }

    idx = ep->index[fd].idx;
    if (idx < 0) {
        idx = ep->nfds++;
        ep->index[fd].idx = idx;
    }

    ep->index[fd].ptr = ptr;

    pf = &ep->pfds[idx];
    pf->fd = fd;
    pf->events = (short)events;

    return 0;
}

static int ef_poll_dissociate(ef_poll_t *p, int fd, int fired, int onclose)
{
    ef_pollsys_t *ep;
    int idx, last;

    ep = (ef_pollsys_t *)p;

    /*
     * fd cannot been store here
     */
    if (fd >= ep->cap) {
        return 0;
    }

    idx = ep->index[fd].idx;
    if (idx < 0) {
        return 0;
    }

    /*
     * set the removed index.idx to -1
     */
    ep->index[fd].idx = -1;

    last = ep->nfds - 1;

    /*
     * only copy data and update index.idx if idx is not the last
     */
    if (idx < last) {
        ep->index[ep->pfds[last].fd].idx = idx;
        memcpy(&ep->pfds[idx], &ep->pfds[last], sizeof(pollfd_t));
    }

    --ep->nfds;

    return 0;
}

static int ef_poll_unset(ef_poll_t *p, int fd, int events)
{
    return 0;
}

static int ef_poll_wait(ef_poll_t *p, ef_event_t *evts, int count, int millisecs)
{
    int ret, idx, cnt;
    ef_pollsys_t *ep = (ef_pollsys_t *)p;

    ret = poll(ep->pfds, ep->nfds, millisecs);
    if (ret <= 0) {
        return ret;
    }

    if (count > ret) {
        count = ret;
    }

    idx = 0;
    cnt = 0;

    while (idx < ep->nfds && cnt < count) {
        if (ep->pfds[idx].revents) {
            evts[cnt].events = ep->pfds[idx].revents;
            evts[cnt].ptr = ep->index[ep->pfds[idx].fd].ptr;
            ++cnt;
        }
        ++idx;
    }

    return cnt;
}

static int ef_poll_free(ef_poll_t *p)
{
    ef_pollsys_t *ep = (ef_pollsys_t *)p;
    free(ep->index);
    free(ep->pfds);
    free(ep);
    return 0;
}

static ef_poll_t *ef_poll_create(int cap)
{
    ef_pollsys_t *ep;
    size_t size = sizeof(ef_pollsys_t);

    ep = (ef_pollsys_t *)malloc(size);
    if (!ep) {
        return NULL;
    }

    /*
     * event buffer at least 128
     */
    if (cap < 128) {
        cap = 128;
    }

    size = sizeof(ef_poll_index_t) * cap;
    ep->index = (ef_poll_index_t *)malloc(size);
    if (!ep->index) {
        free(ep);
        return NULL;
    }

    /*
     * init all index to -1
     */
    memset(ep->index, -1, size);

    size = sizeof(pollfd_t) * cap;
    ep->pfds = (pollfd_t *)malloc(size);
    if (!ep->pfds) {
        free(ep->index);
        free(ep);
        return NULL;
    }

    ep->poll.associate = ef_poll_associate;
    ep->poll.dissociate = ef_poll_dissociate;
    ep->poll.unset = ef_poll_unset;
    ep->poll.wait = ef_poll_wait;
    ep->poll.free = ef_poll_free;
    ep->cap = cap;
    ep->nfds = 0;
    return &ep->poll;
}

create_func_t ef_create_poll = ef_poll_create;
