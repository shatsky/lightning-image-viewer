PREFIX = /usr/local
LDFLAGS = -lm -lSDL3 -lheif
CC = cc

all:
	cargo build --release
	$(CC) $(CFLAGS) -o lightning-image-viewer src/viewer.c target/release/libimage_rs_ffi.a $(LDFLAGS)

clean:
	rm -f lightning-image-viewer

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f lightning-image-viewer $(DESTDIR)$(PREFIX)/bin
	cp -fR share $(DESTDIR)$(PREFIX)

.PHONY: all clean install
