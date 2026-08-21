CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -mwindows
# -lcomdlg32 for Open Dialog, -lshell32 for Drag and Drop
LDFLAGS = -mwindows -lcomdlg32 -lshell32

.PHONY: all clean

all: hexedit.exe

hexedit.exe: hexedit.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o hexedit.exe hexedit.c

clean:
	rm -f hexedit.exe
