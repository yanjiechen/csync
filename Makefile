CC=gcc
CFLAGS=-c -g -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -funroll-loops -ffast-math -fomit-frame-pointer -fno-exceptions
LDFLAGS=-pthread -lghthash -L.
INSTALL=install
INSTALL_DIR=/usr/local

csync: csync.o socket.o log.o conf.o str.o lock.o
	$(CC) -o $@ $^ $(LDFLAGS)

csync.o: csync.c csync.h
	$(CC) $(CFLAGS) -o $@ $<

socket.o: socket.c socket.h
	$(CC) $(CFLAGS) -o $@ $<

log.o: log.c log.h
	$(CC) $(CFLAGS) -o $@ $<

conf.o: conf.c conf.h
	$(CC) $(CFLAGS) -o $@ $<

str.o: str.c str.h
	$(CC) $(CFLAGS) -o $@ $<

lock.o: lock.c lock.h
	$(CC) $(CFLAGS) -o $@ $<

genlist: genlist.o lock.o
	$(CC) -o $@ $^ $(LDFLAGS)

genlist.o: genlist.c
	$(CC) $(CFLAGS) -o $@ $<

install: csync
	$(INSTALL) -g 0 -o 0 -m 0755 -s $< $(INSTALL_DIR)/bin/
	$(INSTALL) -g 0 -o 0 -m 0644 csync.conf $(INSTALL_DIR)/etc/

clean:
	rm -f csync genlist *.o *~
