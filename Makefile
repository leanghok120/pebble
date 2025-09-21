CC = gcc
CFLAGS = -Wall -O2 -I/usr/include/freetype2
LIBS = -lX11 -lXft -lvterm
TARGET = pebble
SRC = pebble.c

all: clean build install

build:
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -rf $(TARGET)

install:
	mv $(TARGET) /usr/local/bin
