PREFIX = /usr/local

all:
	cargo build --release

clean:
	cargo clean

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f target/release/lightning-image-viewer $(DESTDIR)$(PREFIX)/bin
	cp -fR share $(DESTDIR)$(PREFIX)

.PHONY: all clean install
