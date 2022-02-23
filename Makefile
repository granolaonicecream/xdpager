CC=gcc
CFLAGS=-pedantic -Wall
XFT_CFLAGS=$(shell pkg-config --cflags freetype2 xext xinerama)
LDFLAGS=-lX11 -lfontconfig -lXft
XFT_LDFLAGS=$(shell pkg-config --libs freetype2 xext xinerama) -lXft

main: main.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) $(LDFLAGS) $(XFT_LDFLAGS) main.c -o xdpager

clean:
	rm -f xdpager
