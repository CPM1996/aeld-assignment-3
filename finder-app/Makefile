all: writer

writer : writer.o
	${CROSS_COMPILE}${GCC} -o writer writer.o

writer.o : writer.c
	${CROSS_COMPILE}${GCC} -c writer.c

clean :
	rm -f writer writer.o
