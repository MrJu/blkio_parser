CC=gcc
CFLAGS=-Wall -Wextra -Iinclude

all: block

block: block.o list_sort.o
	$(CC) $(CFLAGS) -o block block.o list_sort.o

block.o: block.c
	$(CC) $(CFLAGS) -c block.c

list_sort.o: list_sort.c
	$(CC) $(CFLAGS) -c list_sort.c

clean:
	rm -f block block.o list_sort.o

