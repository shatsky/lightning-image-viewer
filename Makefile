PREFIX = /usr/local
CFLAGS = -DWITH_LIBEXIF
LDFLAGS = -lm -lSDL3 -lSDL3_image -lexif
CC = cc

all:
	$(CC) $(CFLAGS) -o lightning-image-viewer src/viewer.c $(LDFLAGS)

clean:
	rm -f lightning-image-viewer

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f lightning-image-viewer $(DESTDIR)$(PREFIX)/bin
	cp -fR share $(DESTDIR)$(PREFIX)

.PHONY: all clean install
