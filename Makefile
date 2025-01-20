CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -I/usr/include/X11 -I/usr/include/X11/Xft -I/usr/include/freetype2
LDFLAGS = -lX11 -lXft -lfreetype
SRC = src/main.c src/draw.c src/input.c
OBJ = $(SRC:src/%.c=build/%.o)

cupidterminal: $(OBJ)
	$(CC) $(OBJ) -o cupidterminal $(LDFLAGS)

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build cupidterminal
