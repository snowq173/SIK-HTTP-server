CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

.PHONY: serwer clean

serwer: server.o ioprotocol.o request_data.o filesearch.o
	$(CC) $(LDFLAGS) -o $@ $^

ioprotocol.o: ioprotocol.c ioprotocol.h
	$(CC) $(CFLAGS) -c $<

filesearch.o: filesearch.c filesearch.h
	$(CC) $(CFLAGS) -c $<

request_data.o: request_data.c request_data.h
	$(CC) $(CFLAGS) -c $<

server.o: server.c ioprotocol.h request_data.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o serwer
