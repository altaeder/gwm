PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
PKG_CONFIG = pkg-config

CC = cc
CFLAGS = -O2 -std=c99 -Wall -Wextra -I$(PREFIX)/include
LDFLAGS = -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib

PKGS = swc
CFLAGS += `$(PKG_CONFIG) --cflags $(PKGS)`
LDLIBS += `$(PKG_CONFIG) --libs $(PKGS)`

SRC = config.h src/hevel.c src/input.c src/scroll.c src/select.c src/window.c src/zoom.c

all: hevel

config.h:
	cp config.def.h $@

hevel: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o hevel $(SRC) $(LDLIBS)

clean:
	rm -f hevel *.o

confclean:
	rm -f hevel *.o config.h

install: hevel
	install -D -m 755 hevel $(DESTDIR)$(BINDIR)/hevel

.PHONY: clean install FORCE
