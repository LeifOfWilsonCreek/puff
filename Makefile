.PHONY: all install uninstall clean

PREFIX ?= /usr/local
CFLAGS ?= -O3
CFLAGS += -Wall
OBJS = editfile.o keys.o puff.o
HEAD = editfile.h keys.h
LIBS = $(LDFLAGS) -lcurses

all: puff
install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/etc
	install -s -m 0755 puff $(DESTDIR)$(PREFIX)/bin
	install -m 0644 puff.conf $(DESTDIR)$(PREFIX)/etc/puff.conf.sample
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/puff $(DESTDIR)$(PREFIX)/etc/puff.conf.sample
clean:
	rm -f $(OBJS) puff.core core puff testedit.o testedit.core testedit

puff: $(OBJS)
	$(CC) $(CFLAGS) -o puff $(OBJS) $(LIBS)
editfile.o: editfile.c editfile.h
keys.o: keys.c keys.h
puff.o: puff.c $(HEAD)
