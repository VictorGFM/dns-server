all:
	g++ -Wall server.cpp -lpthread -o dns-server
clean:
	rm dns-server