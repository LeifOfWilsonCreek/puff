#MAKECMD = $(CC) -O3
MAKECMD = $(CC) -Wall -g -DDEBUG
PUFFOBJS = shared.o editfile.o keys.o puff.o
PUFFHEAD = shared.h editfile.h keys.h

all: fortune puff
fortune:
	(fortune 2> /dev/null || echo -n)
install: all
	cp puff /usr/local/bin/puff
	if [ ! -e /etc/puff.conf ]; then cp puff.conf /etc; fi
uninstall:
	rm -f /usr/local/bin/puff /etc/puff.conf
clean:
	rm -f $(PUFFOBJS) puff.core core puff testedit.o testedit.core testedit
pack:
	cd .. && rm -f puff.tgz && tar zcvf puff.tgz puff/{Makefile,*.[ch],puff.conf} && cd puff

puff: $(PUFFOBJS)
	$(MAKECMD) -o puff $(PUFFOBJS) -lcurses -lm
shared.o: shared.c shared.h strlcat.c strlcpy.c snprintf.c
	$(MAKECMD) -c shared.c
editfile.o: editfile.c editfile.h
	$(MAKECMD) -c editfile.c
keys.o: keys.c keys.h shared.h
	$(MAKECMD) -c keys.c
puff.o: puff.c $(PUFFHEAD)
	$(MAKECMD) -c puff.c
