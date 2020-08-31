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

#ifndef _EFRAMEWORK_HEADER_
#define _EFRAMEWORK_HEADER_

#include "coroutine.h"
#include "util/list.h"
#include "poll.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#define FD_TYPE_LISTEN 1 // listen
#define FD_TYPE_RWC    2 // read (recv), write (send), connect

typedef struct _ef_routine ef_routine_t;
typedef struct _ef_runtime ef_runtime_t;
typedef struct _ef_queue_fd ef_queue_fd_t;
typedef struct _ef_poll_data ef_poll_data_t;
typedef struct _ef_listen_info ef_listen_info_t;

typedef long (*ef_routine_proc_t)(int fd, ef_routine_t *er);

struct _ef_poll_data {
    int type;
    int fd;
    ef_routine_t *routine_ptr;
    ef_runtime_t *runtime_ptr;
    ef_routine_proc_t ef_proc;
};

struct _ef_queue_fd {
    int fd;
    ef_list_entry_t list_entry;
};

struct _ef_listen_info {
    ef_poll_data_t poll_data;
    ef_routine_proc_t ef_proc;
    ef_list_entry_t list_entry;
    ef_list_entry_t fd_list;
};

struct _ef_runtime {
    ef_poll_t *p;
    int stopping;
    int shrink_millisecs;
    int count_per_shrink;
    ef_coroutine_pool_t co_pool;
    ef_list_entry_t listen_list;
    ef_list_entry_t free_fd_list;
};

struct _ef_routine {
    ef_coroutine_t co;
    ef_poll_data_t poll_data;
};

extern ef_runtime_t *ef_runtime;

#define ef_routine_current() ((ef_routine_t*)ef_coroutine_current(&ef_runtime->co_pool))

int ef_init(ef_runtime_t *rt, size_t stack_size, int limit_min, int limit_max, int shrink_millisecs, int count_per_shrink);
int ef_add_listen(ef_runtime_t *rt, int socket, ef_routine_proc_t ef_proc);
int ef_run_loop(ef_runtime_t *rt);

int ef_routine_close(ef_routine_t *er, int fd);
int ef_routine_connect(ef_routine_t *er, int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t ef_routine_read(ef_routine_t *er, int fd, void *buf, size_t count);
ssize_t ef_routine_write(ef_routine_t *er, int fd, const void *buf, size_t count);
ssize_t ef_routine_recv(ef_routine_t *er, int sockfd, void *buf, size_t len, int flags);
ssize_t ef_routine_send(ef_routine_t *er, int sockfd, const void *buf, size_t len, int flags);

#define ef_wrap_close(fd) \
    ef_routine_close(NULL, fd)

#define ef_wrap_connect(sockfd, addr, addrlen) \
    ef_routine_connect(NULL, sockfd, addr, addrlen)

#define ef_wrap_read(fd, buf, count) \
    ef_routine_read(NULL, fd, buf, count)

#define ef_wrap_write(fd, buf, count) \
    ef_routine_write(NULL, fd, buf, count)

#define ef_wrap_recv(sockfd, buf, len, flags) \
    ef_routine_recv(NULL, sockfd, buf, len, flags)

#define ef_wrap_send(sockfd, buf, len, flags) \
    ef_routine_send(NULL, sockfd, buf, len, flags)

#endif
