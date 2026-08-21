CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11

# Ncurses flags
NCURSES_CFLAGS = $(shell pkg-config --cflags ncursesw 2>/dev/null || pkg-config --cflags ncurses 2>/dev/null)
NCURSES_LIBS = $(shell pkg-config --libs ncursesw 2>/dev/null || pkg-config --libs ncurses 2>/dev/null)

.PHONY: all clean test

all: hexedit.exe hexedit_plugin.dll

hexedit.exe: hexedit.c
	$(CC) $(CFLAGS) $(NCURSES_CFLAGS) -o hexedit.exe hexedit.c $(NCURSES_LIBS)

hexedit_plugin.dll: plugin.c
	$(CC) $(CFLAGS) -shared -o hexedit_plugin.dll plugin.c

clean:
	rm -f hexedit.exe hexedit_plugin.dll test.bin

test: hexedit.exe
	@echo "Running sanity checks..."
	@./hexedit.exe 2>&1 | grep -q "Usage" && echo "Main binary OK." || (echo "Main binary failed." && exit 1)
