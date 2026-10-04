PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
PKG_CONFIG = pkg-config

CC = cc
CFLAGS = -O2 -std=c99 -Wall -Wextra -I$(PREFIX)/include -Ithird_party/neuipc
LDFLAGS = -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib

PKGS = swc
CFLAGS += `$(PKG_CONFIG) --cflags $(PKGS)`
LDLIBS += `$(PKG_CONFIG) --libs $(PKGS)`

SRC = config.h src/hevel.c src/input.c src/scroll.c src/select.c src/window.c src/zoom.c src/ipc.c third_party/neuipc/ipc.c

all: gwm

config.h:
	cp config.def.h $@

gwm: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o gwm $(SRC) $(LDLIBS)

clean:
	rm -f hevel *.o

confclean:
	rm -f hevel *.o config.h

install: gwm
	install -D -m 755 gwm $(DESTDIR)$(BINDIR)/gwm

.PHONY: clean install FORCE
