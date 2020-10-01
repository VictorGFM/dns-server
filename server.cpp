#include <iostream>
#include <string>
#include <map>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define IP_VERSION "v4"

#define ADD_TYPE "add"
#define SEARCH_TYPE "search"
#define LINK_TYPE "link"

#define BUFFER_SIZE 1024

#define REQUEST_TYPE 1
#define RESPONSE_TYPE 2

#define HOSTNAME_NOT_FOUND -1

using namespace std;

void logExit(const char *message);
void createThread(char *port);

void *connection_handler(void *port);
int initializeServerAddress(const char *ipVersion, const char *portstr,
                            struct sockaddr_storage *storage);
int addressParse(const char *addrstr, const char *portstr,
                 struct sockaddr_storage *storage);
//void addrtostr(const struct sockaddr *addr, char *str, size_t strsize);

int serverSocket;
map<string, string> hosts;
map<string, string> links;

int main(int argc, char *argv[]) {
    
    createThread(argv[1]);

    while(true) {
        string commandType;
        
        cout << "Enter a command: ";
        cin >> commandType;

        if(commandType == ADD_TYPE) {
            string hostname, ip;
            cin >> hostname >> ip;
            bool hostFound = hosts.find(hostname) != hosts.end();
            if(hostFound) {
                hosts[hostname] = ip;
                cout << "Hostname: "<< hostname <<" with Ip: "<< ip <<" updated with success!" << endl;
            } else {
                hosts.insert(pair<string, string>(hostname, ip));
                cout << "Hostname: "<< hostname <<" with Ip: "<< ip <<" added with success!" << endl;
            } 
        } else if(commandType == SEARCH_TYPE) {
            string hostname;
            cin >> hostname;
            bool hostFound = hosts.find(hostname) != hosts.end();
            if(hostFound) {
                cout << "Hostname found! With the following Ip: "<< hosts[hostname] << endl;
            } else if(!links.empty()){
                for(auto it=links.begin(); it!=links.end(); it++) {
                    string ip = it->first;
                    string port = it->second;
                    char buffer[BUFFER_SIZE];

                    memset(buffer, 0, BUFFER_SIZE);
                    buffer[0] = REQUEST_TYPE;
                    strcpy(&buffer[1], hostname.c_str()); 

                    struct sockaddr_storage clientAddressStorage;
                    if(addressParse(ip.c_str(), port.c_str(), &clientAddressStorage) != 0) {
                        logExit("Error parsing address.");
                    }

                    struct sockaddr *clientAddress = (struct sockaddr *)(&clientAddressStorage);

                    int sizeMessage = sendto(serverSocket, (const char*)buffer, BUFFER_SIZE, MSG_CONFIRM, 
                                             clientAddress, sizeof(clientAddressStorage));
                    if(sizeMessage < 0) {
                        logExit("Error sending message. (Invalid format)");
                    }
                }
            } else {
                cout << "Hostname not found!" << endl;
            }
        } else if(commandType == LINK_TYPE) {
            string ip, port;
            cin >> ip >> port;
            links.insert(pair<string, string>(ip, port));
            cout << "Link with Ip: "<< ip <<" and Port: "<< port <<" connected with success!" << endl;
        }
    }

    return 0;
}

void logExit(const char *message) {
	printf("%s", message);
	exit(EXIT_FAILURE);
}

void createThread(char *port) {
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, connection_handler, (void*) port) < 0) {
        logExit("Error on creating thread!");
    }
}

void *connection_handler(void *port) {
    struct sockaddr_storage serverAddressStorage, clientAddressStorage;

    memset(&serverAddressStorage, 0, sizeof(serverAddressStorage)); 
    memset(&clientAddressStorage, 0, sizeof(clientAddressStorage)); 

    if(initializeServerAddress(IP_VERSION, (char *)port, &serverAddressStorage) != 0) {
        logExit("Error initializing server address.");
    }

    serverSocket = socket(serverAddressStorage.ss_family, SOCK_DGRAM, 0);
    if (serverSocket == -1) {
        logExit("Error on creating socket!");
    }
    cout << "Server socket created!" << endl;

    int enable = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0) {
        logExit("Error setting socket options.");
    }
    
    struct sockaddr *serverAddress = (struct sockaddr *)(&serverAddressStorage);
    if (bind(serverSocket, serverAddress, sizeof(serverAddressStorage)) != 0) {
        logExit("Error on binding server!");
    }
    cout << "Server binded!" << endl;

    listen(serverSocket, 3);

    while (true) {
        cout << "Waiting for messages..." << endl;
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);

        struct sockaddr *clientAddress = (struct sockaddr *)(&clientAddressStorage);

        socklen_t len = sizeof(clientAddressStorage);
        int sizeMessage = recvfrom(serverSocket, (char*)buffer, BUFFER_SIZE, MSG_WAITALL, 
                                   clientAddress, &len); 
        if(sizeMessage == 0) {
            logExit("Error receiving message. (Invalid format)");
        }
        
        int messageType = (int) buffer[0];
        if(messageType == REQUEST_TYPE) {
            string hostname(&buffer[1]);
            bool hostFound = hosts.find(hostname) != hosts.end();
            if(hostFound) {
                memset(buffer, 0, BUFFER_SIZE);
                buffer[0] = RESPONSE_TYPE;
                strcpy(&buffer[1], hosts[hostname].c_str()); 
                int sizeMessage = sendto(serverSocket, (char*)buffer, BUFFER_SIZE, MSG_CONFIRM, 
                       clientAddress, sizeof(clientAddressStorage));
                if(sizeMessage < 0) {
                    logExit("Error sending message. (Invalid format)");
                }
            } else if(!links.empty()) {
                //send request to linked servers 
            } else {
                memset(buffer, 0, BUFFER_SIZE);
                buffer[0] = RESPONSE_TYPE;
                buffer[1] = -1;
                int sizeMessage = sendto(serverSocket, (char*)buffer, BUFFER_SIZE, MSG_CONFIRM, 
                       clientAddress, sizeof(clientAddressStorage)); 
                if(sizeMessage < 0) {
                    logExit("Error sending message. (Invalid format)");
                }
            }
        } else if(messageType == RESPONSE_TYPE) {
            if(buffer[1] == HOSTNAME_NOT_FOUND) {
                cout << "Hostname not found!" << endl;
            } else {
                string ip(&buffer[1]);
                cout << "Hostname found! With the following Ip: "<< ip << endl;
            }
        } else {
            logExit("Error receiving message. (Invalid message type)");
        }
    }
    
}

int initializeServerAddress(const char *ipVersion, const char *portstr,
                            struct sockaddr_storage *storage) {
    uint16_t port = (uint16_t)atoi(portstr);
    if (port == 0) {
        return -1;
    }
    port = htons(port);

    if (0 == strcmp(ipVersion, "v4")) {
        struct sockaddr_in *serverAddressV4 = (struct sockaddr_in *)storage;
        serverAddressV4->sin_family = AF_INET;
        serverAddressV4->sin_addr.s_addr = INADDR_ANY;
        serverAddressV4->sin_port = port;
        return 0;
    } else if (0 == strcmp(ipVersion, "v6")) {
        struct sockaddr_in6 *serverAddressV6 = (struct sockaddr_in6 *)storage;
        serverAddressV6->sin6_family = AF_INET6;
        serverAddressV6->sin6_addr = in6addr_any;
        serverAddressV6->sin6_port = port;
        return 0;
    } else {
        return -1;
    }
}

int addressParse(const char *addrstr, const char *portstr,
                 struct sockaddr_storage *storage) {
    if (addrstr == NULL || portstr == NULL) {
        return -1;
    }

    uint16_t port = (uint16_t)atoi(portstr);
    if (port == 0) {
        return -1;
    }
    port = htons(port);

    struct in_addr inaddr4;
    if (inet_pton(AF_INET, addrstr, &inaddr4)) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)storage;
        addr4->sin_family = AF_INET;
        addr4->sin_addr = inaddr4;
        addr4->sin_port = port;
        return 0;
    }

    struct in6_addr inaddr6;
    if (inet_pton(AF_INET6, addrstr, &inaddr6)) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)storage;
        addr6->sin6_family = AF_INET6;
        memcpy(&(addr6->sin6_addr), &inaddr6, sizeof(inaddr6));
        addr6->sin6_port = port;
        return 0;
    }

    return -1;
}



/* void addrtostr(const struct sockaddr *addr, char *str, size_t strsize) {
    int version;
    char addrstr[INET6_ADDRSTRLEN + 1] = "";
    uint16_t port;

    if (addr->sa_family == AF_INET) {
        version = 4;
        struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
        if (!inet_ntop(AF_INET, &(addr4->sin_addr), addrstr,
                       INET6_ADDRSTRLEN + 1)) {
            logExit("ntop");
        }
        port = ntohs(addr4->sin_port); // network to host short
    } else if (addr->sa_family == AF_INET6) {
        version = 6;
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
        if (!inet_ntop(AF_INET6, &(addr6->sin6_addr), addrstr,
                       INET6_ADDRSTRLEN + 1)) {
            logExit("ntop");
        }
        port = ntohs(addr6->sin6_port); // network to host short
    } else {
        logExit("unknown protocol family.");
    }
    if (str) {
        snprintf(str, strsize, "IPv%d %s %hu", version, addrstr, port);
    }
} */