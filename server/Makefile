all: aesdsocket

default: aesdsocket

aesdsocket : aesdsocket.o
	${CROSS_COMPILE}${GCC} -o aesdsocket aesdsocket.o

aesdsocket.o : aesdsocket.c
	${CROSS_COMPILE}${GCC} -c aesdsocket.c

clean :
	rm -f aesdsocket aesdsocket.o
