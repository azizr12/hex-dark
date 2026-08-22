CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -mwindows -lcomdlg32 -lshell32 -lgdi32

# Get short commit hash, fallback to 'dev' if not a git repo
COMMIT_ID := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
TARGET = hexedit-$(COMMIT_ID).exe

# Automatically find all .c and .h files in the directory
SRCS = $(wildcard *.c)
HDRS = $(wildcard *.h)

.PHONY: all clean

all: $(TARGET)

# The target depends on both .c and .h files. 
# If ANY source or header file changes, it will recompile automatically.
$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

clean:
	rm -f hexedit-*.exe