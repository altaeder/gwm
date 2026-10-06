PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
PKG_CONFIG = pkg-config

CC = cc
CFLAGS = -O2 -std=c99 -Wall -Wextra -I$(PREFIX)/include -Ithird_party/neuipc
LDFLAGS = -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib

PKGS = swc
CFLAGS += `$(PKG_CONFIG) --cflags $(PKGS)`
LDLIBS += `$(PKG_CONFIG) --libs $(PKGS)`

SRC = config.h src/hevel.c src/input.c src/scroll.c src/select.c src/window.c src/zoom.c src/control.c src/ipc.c third_party/neuipc/ipc.c

all: gwm gc

config.h:
	cp config.def.h $@

gwm: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o GalleryWM $(SRC) $(LDLIBS)

gc: src/galleryControl.c
	$(CC) -Wall -Wextra -pedantic -o gc src/galleryControl.c -lm

clean:
	rm -f gwm gc *.o

confclean:
	rm -f gwm gc *.o config.h

install: gwm gc
	install -D -m 755 GalleryWM $(DESTDIR)$(BINDIR)/GalleryWM
	install -D -m 755 gc $(DESTDIR)$(BINDIR)/gc

.PHONY: clean install FORCE
