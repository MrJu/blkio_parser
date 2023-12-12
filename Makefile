CC=gcc
CFLAGS=-Wall -Wextra -g -Iinclude

all: main

main: main.o trace.o list_sort.o block.o
	$(CC) $(CFLAGS) -o main main.o trace.o list_sort.o block.o

block.o: block.c
	$(CC) $(CFLAGS) -c block.c

list_sort.o: list_sort.c
	$(CC) $(CFLAGS) -c list_sort.c

trace.o: list_sort.c
	$(CC) $(CFLAGS) -c trace.c

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f main main.o trace.o list_sort.o block.o

