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

#include "framework.h"
#include "coroutine.h"
#include "util/list.h"
#include "util/util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

/*
 * the global pointer
 */
ef_runtime_t *ef_runtime = NULL;

inline int ef_queue_fd(ef_runtime_t *rt, ef_listen_info_t *li, int fd) __attribute__((always_inline));
inline int ef_routine_run(ef_runtime_t *rt, ef_routine_proc_t proc, int socket) __attribute__((always_inline));

long ef_proc(void *param)
{
    ef_routine_t *er = (ef_routine_t*)param;
    int fd = er->poll_data.fd;
    long retval = 0;

    if (er->poll_data.ef_proc) {
        retval = er->poll_data.ef_proc(fd, er);
    }

    /*
     * it may or may not closed by the user code
     */
    ef_routine_close(er, fd);

    return retval;
}

inline int ef_routine_run(ef_runtime_t *rt, ef_routine_proc_t proc, int socket)
{
    ef_routine_t *er = (ef_routine_t*)ef_coroutine_create(&rt->co_pool, sizeof(ef_routine_t), ef_proc, NULL);
    if (er) {
        er->poll_data.type = FD_TYPE_RWC;
        er->poll_data.fd = socket;
        er->poll_data.routine_ptr = er;
        er->poll_data.runtime_ptr = rt;
        er->poll_data.ef_proc = proc;
        ef_coroutine_resume(&rt->co_pool, &er->co, 0);
        return 0;
    }
    return -1;
}

inline int ef_queue_fd(ef_runtime_t *rt, ef_listen_info_t *li, int fd)
{
    ef_queue_fd_t *qf;
    int retval, flags;

    flags = fcntl(fd, F_GETFL);
    if (!(flags & O_NONBLOCK)) {
        retval = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        if (retval < 0) {
            return retval;
        }
    }

    if (!ef_list_empty(&rt->free_fd_list)) {
        qf = CAST_PARENT_PTR(ef_list_remove_after(&rt->free_fd_list), ef_queue_fd_t, list_entry);
    } else {
        qf = (ef_queue_fd_t*)malloc(sizeof(ef_queue_fd_t));
    }

    /*
     * close the new connection when no memory
     */
    if (!qf) {
        close(fd);
        return -1;
    }

    qf->fd = fd;
    ef_list_insert_before(&li->fd_list, &qf->list_entry);

    return 0;
}

int ef_init(ef_runtime_t *rt, size_t stack_size, int limit_min, int limit_max, int shrink_millisecs, int count_per_shrink)
{
    ef_poll_t *p = ef_create_poll(1024);
    if (!p) {
        return -1;
    }

    /*
     * the global pointer
     */
    ef_runtime = rt;

    rt->p = p;
    rt->stopping = 0;
    rt->shrink_millisecs = shrink_millisecs;
    rt->count_per_shrink = count_per_shrink;

    if (ef_coroutine_pool_init(&rt->co_pool, stack_size, limit_min, limit_max) < 0) {
        return -1;
    }
    ef_list_init(&rt->listen_list);
    ef_list_init(&rt->free_fd_list);

    return 0;
}

int ef_add_listen(ef_runtime_t *rt, int socket, ef_routine_proc_t proc)
{
    /*
     * set the listen socket in non-block mode
     */
    int retval = fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK);
    if (retval < 0) {
        return retval;
    }

    ef_listen_info_t *li = (ef_listen_info_t*)malloc(sizeof(ef_listen_info_t));
    if (li == NULL) {
        return -1;
    }

    li->poll_data.type = FD_TYPE_LISTEN;
    li->poll_data.fd = socket;
    li->poll_data.routine_ptr = NULL;
    li->poll_data.runtime_ptr = rt;
    li->ef_proc = proc;

    ef_list_init(&li->fd_list);
    ef_list_insert_after(&rt->listen_list, &li->list_entry);

    return 0;
}

int ef_run_loop(ef_runtime_t *rt)
{
    ef_event_t evts[1024];

    /*
     * add all listen socket fds to poll object
     */
    ef_list_entry_t *ent = ef_list_entry_after(&rt->listen_list);
    while (ent != &rt->listen_list) {
        ef_listen_info_t *li = CAST_PARENT_PTR(ent, ef_listen_info_t, list_entry);
        int ret = rt->p->associate(rt->p, li->poll_data.fd, EF_POLLIN, &li->poll_data, 0);
        if (ret < 0) {
            return ret;
        }
        ent = ef_list_entry_after(ent);
    }

    /*
     * the main event loop
     */
    while (1) {
        int cnt = rt->p->wait(rt->p, &evts[0], 1024, 1000);
        if (cnt < 0 && errno != EINTR) {
            return cnt;
        }

        /*
         * check all events returned by poll wait function
         */
        for (int i = 0; i < cnt; ++i) {
            ef_poll_data_t *ed = (ef_poll_data_t*)evts[i].ptr;
            if (ed->type == FD_TYPE_LISTEN) {
                while (1) {
                    int socket = accept(ed->fd, NULL, NULL);
                    if (socket < 0) {
                        rt->p->unset(rt->p, ed->fd, EF_POLLIN);
                        break;
                    }
                    ef_listen_info_t *li = CAST_PARENT_PTR(ed, ef_listen_info_t, poll_data);

                    /*
                     * put new connection to queue
                     */
                    int ret = ef_queue_fd(rt, li, socket);
                    if (ret < 0) {
                        break;
                    }
                }

                /*
                 * solaris event port will auto dissociate fd after event fired
                 */
                rt->p->associate(rt->p, ed->fd, EF_POLLIN, ed, 1);
            } else if (ed->type == FD_TYPE_RWC) {
                ef_coroutine_resume(&rt->co_pool, &ed->routine_ptr->co, evts[i].events);
            }
        }

        /*
         * handle queued connections
         */
        ent = ef_list_entry_after(&rt->listen_list);

        /*
         * every listening sockets
         */
        while (ent != &rt->listen_list) {

            ef_listen_info_t *li = CAST_PARENT_PTR(ent, ef_listen_info_t, list_entry);
            ef_list_entry_t *enf = ef_list_entry_after(&li->fd_list);

            /*
             * every queued connection
             */
            while (enf != &li->fd_list) {

                ef_queue_fd_t *qf = CAST_PARENT_PTR(enf, ef_queue_fd_t, list_entry);
                enf = ef_list_entry_after(enf);

                int ret = ef_routine_run(rt, li->ef_proc, qf->fd);
                if (ret < 0) {
                    goto exit_queue;
                } else {
                    ef_list_remove(&qf->list_entry);
                    ef_list_insert_after(&rt->free_fd_list, &qf->list_entry);
                }
            }
            ent = ef_list_entry_after(ent);
        }

exit_queue:
        if (rt->stopping) {

            /*
             * close all listening socket
             */
            if (!ef_list_empty(&rt->listen_list)) {

                ent = ef_list_entry_after(&rt->listen_list);

                while (ent != &rt->listen_list) {

                    ef_listen_info_t *li = CAST_PARENT_PTR(ent, ef_listen_info_t, list_entry);
                    ent = ef_list_entry_after(ent);

                    /*
                     * close listening socket
                     */
                    if (li->poll_data.fd >= 0) {
                        rt->p->dissociate(rt->p, li->poll_data.fd, 0, 0);
                        close(li->poll_data.fd);
                        li->poll_data.fd = -1;
                    }

                    /*
                     * free listen info if connection queue empty
                     */
                    if (ef_list_empty(&li->fd_list)) {
                        ef_list_remove(&li->list_entry);
                        free(li);
                    }
                }
            }

            /*
             * destroy the unused fd queue item
             */
            if (!ef_list_empty(&rt->free_fd_list)) {
                ent = ef_list_remove_after(&rt->free_fd_list);
                while (ent != NULL) {
                    ef_queue_fd_t *qf = CAST_PARENT_PTR(ent, ef_queue_fd_t, list_entry);
                    ent = ef_list_remove_after(&rt->free_fd_list);
                    free(qf);
                }
            }

            /*
             * shrink coroutine pool, to free
             */
            if (rt->co_pool.free_count == rt->co_pool.full_count) {
                rt->p->free(rt->p);
                ef_coroutine_pool_shrink(&rt->co_pool, 0, -rt->co_pool.full_count);
                break;
            } else {
                ef_coroutine_pool_shrink(&rt->co_pool, 0, -rt->co_pool.free_count);
            }
        }

        if (rt->co_pool.free_count > 0 && rt->co_pool.full_count > rt->co_pool.limit_min) {
            ef_coroutine_pool_shrink(&rt->co_pool, rt->shrink_millisecs, rt->count_per_shrink);
        }
    }
    return 0;
}

int ef_routine_close(ef_routine_t *er, int fd)
{
    if (er == NULL) {
        er = ef_routine_current();
    }

    /*
     * dissociate fd before close
     */
    er->poll_data.runtime_ptr->p->dissociate(er->poll_data.runtime_ptr->p, fd, 0, 1);

    return close(fd);
}

int ef_routine_connect(ef_routine_t *er, int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int retval, flags, error = 0;
    long events;

    if (er == NULL) {
        er = ef_routine_current();
    }

    er->poll_data.type = FD_TYPE_RWC;
    er->poll_data.fd = sockfd;

    /*
     * set non-block mode if needed
     */
    flags = fcntl(sockfd, F_GETFL);
    if (!(flags & O_NONBLOCK)) {
        retval = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        if (retval < 0) {
            error = errno;
            goto exit_conn;
        }
    }

    retval = connect(sockfd, addr, addrlen);
    if (retval < 0) {
        if (errno != EINPROGRESS) {
            error = errno;
            goto exit_conn;
        }
        retval = er->poll_data.runtime_ptr->p->associate(er->poll_data.runtime_ptr->p, sockfd, EF_POLLOUT, &er->poll_data, 0);
        if (retval < 0) {
            error = errno;
            goto exit_conn;
        }
    } else {
        return retval;
    }

    /*
     * yield and wait event
     */
    events = ef_fiber_yield(er->co.fiber.sched, 0);
    if (events & (EF_POLLERR | EF_POLLHUP)) {
        error = EBADF;
        retval = -1;
    } else if (events & EF_POLLOUT) {
        socklen_t len = sizeof(error);
        getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    }

    /*
     * dissociate fd after event fired
     */
    er->poll_data.runtime_ptr->p->dissociate(er->poll_data.runtime_ptr->p, sockfd, 1, 0);

exit_conn:

    errno = error;

    return retval;
}

ssize_t ef_routine_read(ef_routine_t *er, int fd, void *buf, size_t count)
{
    int error = 0;
    long events;
    ssize_t retval;

    if (er == NULL) {
        er = ef_routine_current();
    }

    er->poll_data.type = FD_TYPE_RWC;
    er->poll_data.fd = fd;

    /*
     * always associate
     */
    retval = er->poll_data.runtime_ptr->p->associate(er->poll_data.runtime_ptr->p, fd, EF_POLLIN, &er->poll_data, 0);
    if (retval < 0) {
        return retval;
    } else if (retval > 0) {
        goto ready;
    }

yield:

    /*
     * yield and wait event
     */
    events = ef_fiber_yield(er->co.fiber.sched, 0);
    if (events & EF_POLLERR) {
        error = EBADF;
        retval = -1;
    } else if (events & (EF_POLLIN | EF_POLLHUP)) {
ready:
        retval = read(fd, buf, count);
        if (retval < 0 && errno == EAGAIN) {
            er->poll_data.runtime_ptr->p->unset(er->poll_data.runtime_ptr->p, fd, EF_POLLIN | EF_POLLHUP);
            goto yield;
        } else if (retval < 0) {
            error = errno;
        }
    }

    /*
     * dissociate fd after event fired
     */
    er->poll_data.runtime_ptr->p->dissociate(er->poll_data.runtime_ptr->p, fd, 1, 0);

    errno = error;

    return retval;
}

ssize_t ef_routine_write(ef_routine_t *er, int fd, const void *buf, size_t count)
{
    int error = 0;
    long events;
    ssize_t retval;

    if (er == NULL) {
        er = ef_routine_current();
    }

    er->poll_data.type = FD_TYPE_RWC;
    er->poll_data.fd = fd;

    /*
     * always associate
     */
    retval = er->poll_data.runtime_ptr->p->associate(er->poll_data.runtime_ptr->p, fd, EF_POLLOUT, &er->poll_data, 0);
    if (retval < 0) {
        return retval;
    } else if (retval > 0) {
        goto ready;
    }

yield:

    /*
     * yield and wait event
     */
    events = ef_fiber_yield(er->co.fiber.sched, 0);
    if (events & (EF_POLLERR | EF_POLLHUP)) {
        error = EBADF;
        retval = -1;
    } else if(events & EF_POLLOUT) {
ready:
        retval = write(fd, buf, count);
        if (retval < 0 && errno == EAGAIN) {
            er->poll_data.runtime_ptr->p->unset(er->poll_data.runtime_ptr->p, fd, EF_POLLOUT);
            goto yield;
        } else if (retval < 0) {
            error = errno;
        }
    }

    /*
     * dissociate fd after event fired
     */
    er->poll_data.runtime_ptr->p->dissociate(er->poll_data.runtime_ptr->p, fd, 1, 0);

    errno = error;

    return retval;
}

ssize_t ef_routine_recv(ef_routine_t *er, int sockfd, void *buf, size_t len, int flags)
{
    int retval, error = 0;
    long events;

    if (er == NULL) {
        er = ef_routine_current();
    }

    er->poll_data.type = FD_TYPE_RWC;
    er->poll_data.fd = sockfd;

    /*
     * always associate
     */
    retval = er->poll_data.runtime_ptr->p->associate(er->poll_data.runtime_ptr->p, sockfd, EF_POLLIN, &er->poll_data, 0);
    if (retval < 0) {
        return retval;
    } else if (retval > 0) {
        goto ready;
    }

yield:

    /*
     * yield and wait event
     */
    events = ef_fiber_yield(er->co.fiber.sched, 0);
    if (events & EF_POLLERR) {
        error = EBADF;
        retval = -1;
    } else if (events & (EF_POLLIN | EF_POLLHUP)) {
ready:
        retval = recv(sockfd, buf, len, flags);
        if (retval < 0 && errno == EAGAIN) {
            er->poll_data.runtime_ptr->p->unset(er->poll_data.runtime_ptr->p, sockfd, EF_POLLIN | EF_POLLHUP);
            goto yield;
        } else if (retval < 0) {
            error = errno;
        }
    }

    /*
     * dissociate fd after event fired
     */
    er->poll_data.runtime_ptr->p->dissociate(er->poll_data.runtime_ptr->p, sockfd, 1, 0);

    errno = error;

    return retval;
}

ssize_t ef_routine_send(ef_routine_t *er, int sockfd, const void *buf, size_t len, int flags)
{
    int retval, error = 0;
    long events;

    if (er == NULL) {
        er = ef_routine_current();
    }

    er->poll_data.type = FD_TYPE_RWC;
    er->poll_data.fd = sockfd;
    /*
     * always associate
     */
    retval = er->poll_data.runtime_ptr->p->associate(er->poll_data.runtime_ptr->p, sockfd, EF_POLLOUT, &er->poll_data, 0);
    if (retval < 0) {
        return retval;
    } else if (retval > 0) {
        goto ready;
    }

yield:

    /*
     * yield and wait event
     */
    events = ef_fiber_yield(er->co.fiber.sched, 0);
    if (events & (EF_POLLERR | EF_POLLHUP)) {
        error = EBADF;
        retval = -1;
    } else if(events & EF_POLLOUT) {
ready:
        retval = send(sockfd, buf, len, flags);
        if (retval < 0 && errno == EAGAIN) {
            er->poll_data.runtime_ptr->p->unset(er->poll_data.runtime_ptr->p, sockfd, EF_POLLOUT);
            goto yield;
        } else if (retval < 0) {
            error = errno;
        }
    }

    /*
     * dissociate fd after event fired
     */
    er->poll_data.runtime_ptr->p->dissociate(er->poll_data.runtime_ptr->p, sockfd, 1, 0);

    errno = error;

    return retval;
}
