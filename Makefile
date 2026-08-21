CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11
# -lcomdlg32 for Open/Save Dialogs, -lshell32 for Drag and Drop, -lgdi32 for GDI rendering
LDFLAGS = -mwindows -lcomdlg32 -lshell32 -lgdi32 -luxtheme

.PHONY: all clean

all: hexedit.exe

# Compile both .c files together and link against the required Windows libraries
hexedit.exe: gui.c hex.c hex.h
	$(CC) $(CFLAGS) gui.c hex.c $(LDFLAGS) -o hexedit.exe

clean:
	rm -f hexedit.exe