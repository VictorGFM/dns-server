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
void createSocket(struct sockaddr_storage *serverAddressStorage);
void bindServer(struct sockaddr_storage *serverAddressStorage);
void receiveMessage(char *buffer, struct sockaddr_storage *clientAddressStorage);
void sendIpFromHostnameFound(char *buffer, struct sockaddr_storage *clientAddressStorage, string hostname);
void sendHostNotFound(char *buffer, struct sockaddr_storage *clientAddressStorage);
string searchHostOnLinkedServers(string hostname);
void addHost(string hostname, string ip);
void searchHost(string hostname);
int addressParse(const char *addrstr, const char *portstr,
                 struct sockaddr_storage *storage);
void connectLink(string ip, string port);

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
            addHost(hostname, ip);
        } else if(commandType == SEARCH_TYPE) {
            string hostname;
            cin >> hostname;
            searchHost(hostname);
        } else if(commandType == LINK_TYPE) {
            string ip, port;
            cin >> ip >> port;
            connectLink(ip, port);
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

    createSocket(&serverAddressStorage);

    bindServer(&serverAddressStorage);
    
    listen(serverSocket, 3);

    while (true) {
        cout << "Waiting for messages..." << endl;
        char buffer[BUFFER_SIZE];
        
        receiveMessage(buffer, &clientAddressStorage);
        
        int messageType = (int) buffer[0];
        if(messageType == REQUEST_TYPE) {
            string hostname(&buffer[1]);
            bool hostFound = hosts.find(hostname) != hosts.end();
            if(hostFound) {
                sendIpFromHostnameFound(buffer, &clientAddressStorage, hostname);
            } else if(!links.empty()) {
                string result = searchHostOnLinkedServers(hostname);
                //send response with result (NOT_FOUND or IP FOUND)
            } else {
                sendHostNotFound(buffer, &clientAddressStorage);
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

void createSocket(struct sockaddr_storage *serverAddressStorage) {
    serverSocket = socket(serverAddressStorage->ss_family, SOCK_DGRAM, 0);
    if (serverSocket == -1) {
        logExit("Error on creating socket!");
    }
    cout << "Server socket created!" << endl;

    int enable = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0) {
        logExit("Error setting socket options.");
    }
}

void bindServer(struct sockaddr_storage *serverAddressStorage) {
    struct sockaddr *serverAddress = (struct sockaddr *)(serverAddressStorage);
    if (bind(serverSocket, serverAddress, sizeof(*serverAddressStorage)) != 0) {
        logExit("Error on binding server!");
    }
    cout << "Server binded!" << endl;
}

void receiveMessage(char *buffer, struct sockaddr_storage *clientAddressStorage) {
    memset(buffer, 0, BUFFER_SIZE);
    struct sockaddr *clientAddress = (struct sockaddr *)(clientAddressStorage);
    socklen_t len = sizeof(*clientAddressStorage);
    int sizeMessage = recvfrom(serverSocket, (char*)buffer, BUFFER_SIZE, MSG_WAITALL, 
                               clientAddress, &len); 
    if(sizeMessage == 0) {
        logExit("Error receiving message. (Invalid format)");
    }
}

void sendIpFromHostnameFound(char *buffer, struct sockaddr_storage *clientAddressStorage, 
                             string hostname) {
    memset(buffer, 0, BUFFER_SIZE);
    buffer[0] = RESPONSE_TYPE;
    strcpy(&buffer[1], hosts[hostname].c_str()); 
    struct sockaddr *clientAddress = (struct sockaddr *)(clientAddressStorage);
    int sizeMessage = sendto(serverSocket, (char*)buffer, BUFFER_SIZE, MSG_CONFIRM, 
        clientAddress, sizeof(*clientAddressStorage));
    if(sizeMessage < 0) {
        logExit("Error sending message. (Invalid format)");  
    }
}

void sendHostNotFound(char *buffer, struct sockaddr_storage *clientAddressStorage) {
    memset(buffer, 0, BUFFER_SIZE);
    buffer[0] = RESPONSE_TYPE;
    buffer[1] = HOSTNAME_NOT_FOUND;
    struct sockaddr *clientAddress = (struct sockaddr *)(clientAddressStorage);
    int sizeMessage = sendto(serverSocket, (char*)buffer, BUFFER_SIZE, MSG_CONFIRM, 
        clientAddress, sizeof(*clientAddressStorage)); 
    if(sizeMessage < 0) {
        logExit("Error sending message. (Invalid format)");
    }
}

void addHost(string hostname, string ip) {
    bool hostFound = hosts.find(hostname) != hosts.end();
    if(hostFound) {
        hosts[hostname] = ip;
        cout << "Hostname: "<< hostname <<" with Ip: "<< ip <<" updated with success!" << endl;
    } else {
        hosts.insert(pair<string, string>(hostname, ip));
        cout << "Hostname: "<< hostname <<" with Ip: "<< ip <<" added with success!" << endl;
    } 
}

void searchHost(string hostname) {
    bool hostFound = hosts.find(hostname) != hosts.end();
    if(hostFound) {
        cout << "Hostname found! With the following Ip: "<< hosts[hostname] << endl;
    } else if(!links.empty()){
        string result = searchHostOnLinkedServers(hostname);
        if(result == to_string(HOSTNAME_NOT_FOUND)) {
            cout << "Hostname not found!" << endl;
        } else {
            cout << "Hostname found! With the following Ip: "<< result << endl;
        }
    } else {
        cout << "Hostname not found!" << endl;
    }
}

string searchHostOnLinkedServers(string hostname) {
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
            logExit("Error sending message.");
        }

        receiveMessage(buffer, &clientAddressStorage);

        if(buffer[1] != HOSTNAME_NOT_FOUND) {
            string ip(&buffer[1]);
            return ip;
        }
    }
    return to_string(HOSTNAME_NOT_FOUND);
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

void connectLink(string ip, string port) {
    links.insert(pair<string, string>(ip, port));
    cout << "Link with Ip: "<< ip <<" and Port: "<< port <<" connected with success!" << endl;
}