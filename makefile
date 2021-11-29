# makefile

all: server client

common.o: common.h common.cpp
	g++ -g -w -std=c++11 -c common.cpp

Histogram.o: Histogram.h Histogram.cpp
	g++ -g -w -std=c++11 -c Histogram.cpp

TCPRequestChannel.o: TCPRequestChannel.h TCPRequestChannel.cpp
	g++ -g -w -std=c++11 -c TCPRequestChannel.cpp

client: client.cpp Histogram.o TCPRequestChannel.o common.o
	g++ -g -fsanitize=address -fno-omit-frame-pointer -w -std=c++11 -o client client.cpp Histogram.o TCPRequestChannel.o common.o -lpthread -lrt

server: server.cpp  TCPRequestChannel.o common.o
	g++ -g -fsanitize=address -fno-omit-frame-pointer -w -std=c++11 -o server server.cpp TCPRequestChannel.o common.o -lpthread -lrt

clean:
	rm -rf *.o fifo* server client 
