CC=gcc
CFLAGS=-pedantic -Wall
XFT_CFLAGS=$(shell pkg-config --cflags freetype2)
LDFLAGS=-lX11 -lfontconfig -lXft
XFT_LDFLAGS=$(shell pkg-config --libs freetype2) -lXft

main: main.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) $(LDFLAGS) $(XFT_LDFLAGS) main.c -o pager

clean:
	rm -f main
