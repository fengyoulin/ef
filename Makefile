all: prog_poll clean_tmp

linux: prog_poll prog_epoll prog_epollet clean_tmp

macos: prog_poll prog_kqueue clean_tmp

solaris: prog_poll prog_port clean_tmp

prog_poll: main.c poll.c framework.c coroutine.c fiber.c /tmp/fiber.s
	gcc -g -m64 -std=gnu99 -o prog_poll main.c poll.c framework.c coroutine.c fiber.c /tmp/fiber.s

prog_kqueue: main.c kqueue.c framework.c coroutine.c fiber.c /tmp/fiber.s
	gcc -g -m64 -std=gnu99 -o prog_kqueue main.c kqueue.c framework.c coroutine.c fiber.c /tmp/fiber.s

prog_epoll: main.c epoll.c framework.c coroutine.c fiber.c /tmp/fiber.s
	gcc -g -m64 -std=gnu99 -o prog_epoll main.c epoll.c framework.c coroutine.c fiber.c /tmp/fiber.s

prog_epollet: main.c epollet.c framework.c coroutine.c fiber.c /tmp/fiber.s
	gcc -g -m64 -std=gnu99 -o prog_epollet main.c epollet.c framework.c coroutine.c fiber.c /tmp/fiber.s

prog_port: main.c port.c framework.c coroutine.c fiber.c /tmp/fiber.s
	gcc -g -m64 -std=gnu99 -o prog_port main.c port.c framework.c coroutine.c fiber.c /tmp/fiber.s

/tmp/fiber.s: amd64/fiber.s
	if [[ "$$(uname -a)" =~ "Darwin" ]]; then cat amd64/fiber.s | sed 's/ef_fiber_internal_swap/_ef_fiber_internal_swap/g' | sed 's/ef_fiber_internal_init/_ef_fiber_internal_init/g' > /tmp/fiber.s; else cp amd64/fiber.s /tmp/fiber.s; fi

clean_tmp:
	rm /tmp/fiber.s

clean:
	rm -rf prog_* /tmp/fiber.s
