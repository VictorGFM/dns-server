all:
	g++ -Wall server.cpp -lpthread -o servidor_dns
clean:
	rm servidor_dns