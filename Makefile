CC = gcc
CFLAGS = -Wall -Wextra -Isrc/include -Isrc/app/include
LDSCRIPT = script.lds
TARGET = main
BUILDDIR = build
VPATH = src:src/app

SRCS = $(wildcard src/*.c) $(wildcard src/app/*.c)
OBJS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS))

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -T $(LDSCRIPT)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)/app

clean:
	rm -rf $(BUILDDIR) $(TARGET)

