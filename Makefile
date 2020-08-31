all: prog_poll

linux: prog_poll prog_epoll prog_epollet

solaris: prog_poll prog_port

prog_poll: main.c poll.c framework.c coroutine.c fiber.c amd64/fiber.o
	gcc -g -m64 -std=gnu99 -o prog_poll main.c poll.c framework.c coroutine.c fiber.c amd64/fiber.o

prog_epoll: main.c epoll.c framework.c coroutine.c fiber.c amd64/fiber.o
	gcc -g -m64 -std=gnu99 -o prog_epoll main.c epoll.c framework.c coroutine.c fiber.c amd64/fiber.o

prog_epollet: main.c epollet.c framework.c coroutine.c fiber.c amd64/fiber.o
	gcc -g -m64 -std=gnu99 -o prog_epollet main.c epollet.c framework.c coroutine.c fiber.c amd64/fiber.o

prog_port: main.c port.c framework.c coroutine.c fiber.c amd64/fiber.o
	gcc -g -m64 -std=gnu99 -o prog_port main.c port.c framework.c coroutine.c fiber.c amd64/fiber.o

amd64/fiber.o: amd64/fiber.s
	as -g --64 -o amd64/fiber.o amd64/fiber.s

clean:
	rm -f prog_* amd64/fiber.o
