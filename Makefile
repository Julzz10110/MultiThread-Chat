CC      = g++
CFLAGS  = -O3
OPTION  = -std=c++17
LIBS    = -lboost_system -lboost_thread -pthread

all: server client

server: server.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

client: client.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)
	
server.o: server.cpp protocol.hpp
	$(CC) $(OPTION) -c $(CFLAGS) server.cpp

client.o: client.cpp protocol.hpp
	$(CC) $(OPTION) -c $(CFLAGS) client.cpp

.PHONY: clean

clean:
	rm *.o
	rm server client