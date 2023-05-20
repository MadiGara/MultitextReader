CC = gcc
CFLAGS = -c -Wall -std=c11 -pedantic -g

all: main

main: A2.o
	$(CC) A2.o -o main
A2.o: A2.c
	$(CC) $(CFLAGS) A2.c -o A2.o
clean:
	rm *.o *.hist main