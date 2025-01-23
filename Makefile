# Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -fPIC -I/usr/include/X11 -I/usr/include/X11/Xft -I/usr/include/freetype2
LDFLAGS = -lX11 -lXft -lfreetype -lutf8proc -lfontconfig

SRCS = src/main.c src/draw.c src/input.c src/terminal_state.c
OBJS = $(SRCS:src/%.c=build/%.o)
TARGET = cupidterminal

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build $(TARGET)
