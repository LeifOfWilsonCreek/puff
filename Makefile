PREFIX ?= /usr/local
CFLAGS += -Wall -O3
#CFLAGS += -DDEBUG -g
OBJS = editfile.o keys.o puff.o
HEAD = editfile.h keys.h
LIBS = -lcurses

all: puff
install: all
	install -m 0755 puff $(PREFIX)/bin
	mkdir -p $(PREFIX)/etc || echo -n
	install -m 0644 puff.conf $(PREFIX)/etc/puff.conf.sample
uninstall:
	rm -f $(PREFIX)/bin/puff $(PREFIX)/etc/puff.conf
clean:
	rm -f $(OBJS) puff.core core puff testedit.o testedit.core testedit

puff: $(OBJS)
	$(CC) $(CFLAGS) -o puff $(OBJS) $(LIBS)
editfile.o: editfile.c editfile.h
keys.o: keys.c keys.h
puff.o: puff.c $(HEAD)
