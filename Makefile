CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -mwindows -lcomdlg32 -lshell32 -lgdi32 -luxtheme

# Automatically gather all .c and .h files in the current directory
SRCS := $(wildcard *.c)
HDRS := $(wildcard *.h)

TARGET = hexedit.exe

.PHONY: all clean

all: $(TARGET)

# Rebuild if any source or header file changes
$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)