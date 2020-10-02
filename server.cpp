#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <list>
#include <cstring>
#include<ios>
#include<limits>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define IP_VERSION "v4" //v4 or v6

#define ADD_TYPE "add"
#define SEARCH_TYPE "search"
#define LINK_TYPE "link"

#define BUFFER_SIZE 1024

#define REQUEST_TYPE 1
#define RESPONSE_TYPE 2

#define HOSTNAME_NOT_FOUND -1

#define SET_TIMEOUT true

using namespace std;

void logExit(const char *message);
void validateInputParameters(int argc, char *argv[]);
void createThread(char *port);
void *connection_handler(void *port);
int initializeServerAddress(const char *ipVersion, const char *portstr,
                            struct sockaddr_storage *storage);
int createSocket(struct sockaddr_storage *serverAddressStorage, bool isSetTimeout);
void bindServer(int socket_desc, struct sockaddr_storage *serverAddressStorage);
int receiveMessage(int socket_desc, char *buffer, struct sockaddr_storage *addressStorage);
int sendMessage(int socket_desc, char *buffer, struct sockaddr_storage *addressStorage);
string searchHostOnLinkedServers(string hostname);
void addHost(string hostname, string ip);
void searchHost(string hostname);
int addressParse(const char *addrstr, const char *portstr,
                 struct sockaddr_storage *storage);
void connectLink(string ip, string port);

struct link {
    pair<string, string> linkData; //<ip, port>
    int socket_desc;
    struct sockaddr_storage storage;
};

map<string, string> hosts;
list<struct link> links;

int main(int argc, char *argv[]) {

    validateInputParameters(argc, argv);

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
        } else {
            cout << "Invalid command type!" << endl;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
        }
    }

    return 0;
}

void logExit(const char *message) {
	printf("%s", message);
	exit(EXIT_FAILURE);
}

void validateInputParameters(int argc, char *argv[]) {
    if (argc < 2) {
		logExit("Invalid Arguments. Inform a server port.\nExample: 5151\n");
    } else if(argc == 3) {
        string fileName(argv[2]);
        ifstream file(fileName);
        if(!file) {
            logExit("Error trying to read file!");
        }
        while (!file.eof( )) {
            string commandType;
            file >> commandType;
            if(commandType == ADD_TYPE) {
                string hostname, ip;
                file >> hostname >> ip;
                addHost(hostname, ip);
            } else if(commandType == LINK_TYPE) {
                string ip, port;
                file >> ip >> port;
                connectLink(ip, port);
            } else {
                logExit("Invalid command type while reading file!");
            }
        }
        file.close();
    }
}

void createThread(char *port) {
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, connection_handler, (void*) port) < 0) {
        logExit("Error on creating thread!");
    }
}

void *connection_handler(void *port) {
    struct sockaddr_storage serverAddressStorage, addressStorage;

    memset(&serverAddressStorage, 0, sizeof(serverAddressStorage)); 
    memset(&addressStorage, 0, sizeof(addressStorage)); 

    if(initializeServerAddress(IP_VERSION, (char *)port, &serverAddressStorage) != 0) {
        logExit("Error initializing server address.");
    }

    int socket_desc = createSocket(&serverAddressStorage, !SET_TIMEOUT);

    bindServer(socket_desc, &serverAddressStorage);
    
    listen(socket_desc, 3);

    while (true) {
        char buffer[BUFFER_SIZE];
        
        receiveMessage(socket_desc, buffer, &addressStorage);
        
        int messageType = (int) buffer[0];
        if(messageType == REQUEST_TYPE) {
            string hostname(&buffer[1]);
            bool hostFound = hosts.find(hostname) != hosts.end();
            if(hostFound) {
                memset(buffer, 0, BUFFER_SIZE);
                buffer[0] = RESPONSE_TYPE;
                strcpy(&buffer[1], hosts[hostname].c_str()); 

                sendMessage(socket_desc, buffer, &addressStorage);
            } else if(!links.empty()) {
                string result = searchHostOnLinkedServers(hostname);

                memset(buffer, 0, BUFFER_SIZE);
                buffer[0] = RESPONSE_TYPE;
                if(result == to_string(HOSTNAME_NOT_FOUND)) {
                    buffer[1] = HOSTNAME_NOT_FOUND;
                    buffer[2] = '\0';
                } else {
                    strcpy(&buffer[1], result.c_str()); 
                }

                sendMessage(socket_desc, buffer, &addressStorage);
            } else {
                memset(buffer, 0, BUFFER_SIZE);
                buffer[0] = RESPONSE_TYPE;
                buffer[1] = HOSTNAME_NOT_FOUND;
                buffer[2] = '\0';

                sendMessage(socket_desc, buffer, &addressStorage);
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

int createSocket(struct sockaddr_storage *serverAddressStorage, bool isSetTimeout) {
    int socket_desc = socket(serverAddressStorage->ss_family, SOCK_DGRAM, 0);
    if (socket_desc == -1) {
        logExit("Error on creating socket!");
    }

    if(isSetTimeout) {
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        if (setsockopt(socket_desc, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
            logExit("Error setting socket options.");
        }
    }else {
        int enable = 1;
        if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0) {
            logExit("Error setting socket options.");
        }
    }

    return socket_desc;
}

void bindServer(int socket_desc, struct sockaddr_storage *serverAddressStorage) {
    struct sockaddr *serverAddress = (struct sockaddr *)(serverAddressStorage);
    if (bind(socket_desc, serverAddress, sizeof(*serverAddressStorage)) != 0) {
        logExit("Error on binding server!");
    }
}

int receiveMessage(int socket_desc, char *buffer, struct sockaddr_storage *addressStorage) {
    memset(buffer, 0, BUFFER_SIZE);
    struct sockaddr *address = (struct sockaddr *)(addressStorage);
    socklen_t len = sizeof(*addressStorage);
    int sizeMessage = recvfrom(socket_desc, (char*)buffer, BUFFER_SIZE, MSG_WAITALL, 
                               address, &len); 
    if(sizeMessage == 0) {
        logExit("Error receiving message. (Invalid format)");
    }
    return sizeMessage;
}

int sendMessage(int socket_desc, char *buffer, struct sockaddr_storage *addressStorage) {
    struct sockaddr *address = (struct sockaddr *)(addressStorage);
    int sizeMessage = sendto(socket_desc, (char*)buffer, BUFFER_SIZE, MSG_CONFIRM, 
        address, sizeof(*addressStorage));
    if(sizeMessage < 0) {
        logExit("Error sending message. (Invalid format)");  
    }
    return sizeMessage;
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
    for(auto link : links) {
        int socket_desc = link.socket_desc;
        struct sockaddr_storage addressStorage = link.storage;

        char buffer[BUFFER_SIZE];

        memset(buffer, 0, BUFFER_SIZE);
        buffer[0] = REQUEST_TYPE;
        strcpy(&buffer[1], hostname.c_str()); 
        
        struct sockaddr *address = (struct sockaddr *)(&addressStorage);

        int sizeMessage = sendto(socket_desc, (const char*)buffer, BUFFER_SIZE, MSG_CONFIRM, 
                                    address, sizeof(addressStorage));
        if(sizeMessage < 0) {
            logExit("Error sending message.");
        }

        sizeMessage = receiveMessage(socket_desc, buffer, &addressStorage);
        
        bool isTimeout = sizeMessage < 0;
        if(!isTimeout && buffer[1] != HOSTNAME_NOT_FOUND) {
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
    pair<string, string> newLinkData = make_pair(ip, port);

    bool linkExists = false;
    for (auto link : links) {
        if (link.linkData == newLinkData) {
            linkExists = true;
            break;
        }
    }

    if(linkExists) {
        cout << "Link already exists!" << endl;
    } else {
        struct sockaddr_storage serverAddressStorage;
        if(addressParse(ip.c_str(), port.c_str(), &serverAddressStorage) != 0) {
            logExit("Error parsing address.");
        }
        int socket_desc = createSocket(&serverAddressStorage, SET_TIMEOUT);

        struct link link;
        link.linkData = newLinkData;
        link.socket_desc = socket_desc;
        link.storage = serverAddressStorage;

        links.push_back(link);
        cout << "Link with Ip: "<< ip <<" and Port: "<< port <<" connected with success!" << endl;
    }
}