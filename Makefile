CC = gcc
# -mwindows hides the console window. -lcomdlg32 is for the Open File dialog.
CFLAGS = -O2 -Wall -Wextra -std=c11 -mwindows
LDFLAGS = -mwindows -lcomdlg32

.PHONY: all clean

all: hexedit.exe

hexedit.exe: hexedit.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o hexedit.exe hexedit.c

clean:
	rm -f hexedit.exe
