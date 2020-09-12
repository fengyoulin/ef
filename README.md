# Easy-event Framework 核心版 #

提供了一种协程（池）的实现，以及基于IO多路复用的协程调度，目的是通过封装屏蔽复杂的事件循环以及平台相关的api，使应用程序在享受IO多路复用带来的高吞吐量的同时，保持socket操作的简单性。

## 编译运行 ##

目前项目支持的IO多路复用形式包括：poll、epoll、epollet、kqueue、event port。可在编译时指定具体IO多路复用形式：

```
make prog_poll     // all unix like
make prog_epoll    // linux
make prog_epollet  // linux
make prog_kqueue   // macos, freebsd
make prog_port     // solaris
```

也可指定平台，Linux下会编译poll、epoll、epollet三个版本；macos会编译kqueue；solaris会编译event port。

```
make linux
make macos
make solaris
```

编译后直接运行即可，目前`main.c`中实现的业务逻辑是这样的，监听8080端口，将请求转发至80端口，而80端口的监听程序会返回一句问候语。

```
Welcome to the EFramework!
```

## 性能测试 ##

```
$ ab -n 10000 -c 100 -H 'Connection: Close' http://127.0.0.1:8080/
This is ApacheBench, Version 2.3 <$Revision: 1807734 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking 127.0.0.1 (be patient)
Completed 1000 requests
Completed 2000 requests
Completed 3000 requests
Completed 4000 requests
Completed 5000 requests
Completed 6000 requests
Completed 7000 requests
Completed 8000 requests
Completed 9000 requests
Completed 10000 requests
Finished 10000 requests


Server Software:        
Server Hostname:        127.0.0.1
Server Port:            8080

Document Path:          /
Document Length:        26 bytes

Concurrency Level:      100
Time taken for tests:   1.134 seconds
Complete requests:      10000
Failed requests:        0
Total transferred:      1250000 bytes
HTML transferred:       260000 bytes
Requests per second:    8821.18 [#/sec] (mean)
Time per request:       11.336 [ms] (mean)
Time per request:       0.113 [ms] (mean, across all concurrent requests)
Transfer rate:          1076.80 [Kbytes/sec] received

Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0    0   0.4      0       6
Processing:     2   11   1.7     11      24
Waiting:        2   11   1.7     10      23
Total:          8   11   1.6     11      24

Percentage of the requests served within a certain time (ms)
  50%     11
  66%     11
  75%     12
  80%     12
  90%     13
  95%     14
  98%     15
  99%     20
 100%     24 (longest request)
```

## 目录结构 ##

```
├-- amd64
│   └-- fiber.s   // 汇编实现协程初始化与切换等底层逻辑
├-- i386
│   └-- fiber.s
├-- util
├-- coroutine.h
├-- coroutine.c   // 实现协程池，简化了协程的管理
├-- fiber.h
├-- fiber.c       // 实现了协程，提供核心API
├-- framework.h
├-- framework.c   // 框架层，封装了事件循环，实现了基于IO的协程调度
├-- epoll.c
├-- epollet.c     // edge triger
├-- kqueue.c
├-- poll.c        // 基本上所有Unix系统都会支持poll
├-- poll.h
├-- port.c        // event port
├-- main.c
├-- Makefile
└-- Makefile.i386
```

## 示例浅析 ##

1. 首先要进行框架初始化，包括协程池初始化与IO多路复用初始化工作。
2. 然后创建用于监听端口的socket并加入到框架中存储监听类型socket的链表中，并指定业务处理入口。
3. 最后运行框架，开始IO多路复用的事件循环就可以了。

以下示例来自`main.c`：

```
int main(int argc, char *argv[])
{
    // 1. 初始化框架
    // 协程池初始化，需要指定协程池规模，协程栈大小
    // IO多路复用初始化
    if (ef_init(&efr, 64 * 1024, 256, 512, 1000 * 60, 16) < 0) {
        return -1;
    }

    ......

    // 2. 创建监听socket
    // 监听8080端口
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
    {
        return -1;
    }
    struct sockaddr_in addr_in = {0};
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(8080);
    int retval = bind(sockfd, (const struct sockaddr *)&addr_in, sizeof(addr_in));
    if(retval < 0)
    {
        return -1;
    }
    listen(sockfd, 512);

    // 把socket加入监听socket链表
    // 框架支持多个监听socket分别监听不同端口，所以先放入链表，框架运行起来后会一并处理
    // 需要指定业务处理入口，此处为forward_proc
    // 新建立的连接会交给一个协程，forward_proc便是这些协程的执行入口
    ef_add_listen(&efr, sockfd, forward_proc);

    ......

    // 3. 运行框架，开启IO多路复用事件循环
    return ef_run_loop(&efr);
}
```

接下来我们要做的就是实现forward_proc等业务处理函数，在其中使用框架包装好的IO操作函数，就可以按照常规业务逻辑来编写，完全不用关心协程切换与IO事件注册。

```
// 将8080端口接收到的GET请求转发到80端口
long forward_proc(int fd, ef_routine_t *er)
{
    char buffer[BUFFER_SIZE];
    // 读请求，理论上对于GET一次read应该就可以
    ssize_t r = ef_routine_read(er, fd, buffer, BUFFER_SIZE);
    if(r <= 0)
    {
        return r;
    }

    // 建立到80端口的连接
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr_in = {0};
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(80);
    int ret = ef_routine_connect(er, sockfd, (const struct sockaddr *)&addr_in, sizeof(addr_in));
    if(ret < 0)
    {
        return ret;
    }

    // 将读取到的请求体发送到80端口
    ssize_t w = ef_routine_write(er, sockfd, buffer, r);
    if(w < 0)
    {
        goto exit_proc;
    }

    while(1)
    {
        // 从80端口循环读取响应数据
        r = ef_routine_read(er, sockfd, buffer, BUFFER_SIZE);
        if(r <= 0)
        {
            break;
        }
        ssize_t wrt = 0;

        // 将响应数据写给请求方，循环确保完全写入
        while(wrt < r)
        {
            w = ef_routine_write(er, fd, &buffer[wrt], r - wrt);
            if(w < 0)
            {
                goto exit_proc;
            }
            wrt += w;
        }
    }
exit_proc:
    ef_routine_close(er, sockfd);
    return ret;
}
```
