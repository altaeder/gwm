PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

CC = cc
CFLAGS = -O2 -std=c99 -Wall -Wextra -I$(PREFIX)/include
LDFLAGS = -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib

PKGS = swc wayland-server libinput pixman-1 xkbcommon libdrm wld libudev xcb xcb-composite xcb-ewmh xcb-icccm
CFLAGS += `pkg-config --cflags $(PKGS)`
LDLIBS += `pkg-config --libs $(PKGS)`

SRC = src/hevel.c src/input.c src/scroll.c src/select.c src/window.c src/zoom.c

all: hevel

hevel: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o hevel $(SRC) $(LDLIBS)

clean:
	rm -f hevel *.o

install: hevel
	install -D -m 755 hevel $(DESTDIR)$(BINDIR)/hevel

.PHONY: clean install FORCE
