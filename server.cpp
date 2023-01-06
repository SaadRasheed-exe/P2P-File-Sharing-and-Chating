#include <string.h>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define SERVER_PORT_NO 30000
#define CLIENT_LIMIT 10
long selfSock;

map<short, string> clientAddr;
map<short, long> clientSock;
map<short, pthread_t> clientThread;
short clientCount = 0;

bool initIssueHandled = false;

// prints addresses and port numbers of all the connected clients
void printAddrs()
{
    cout << "Connected clients: " << endl;
    for (auto i = clientAddr.begin(); i != clientAddr.end(); i++)
    {
        cout << i->first << ' ' <<  i->second << endl;
    }
}

// first client to connect to a server will have address 0.0.0.0:0
// handleInitIssue will connect a dummy client which will later disconnect
void* handleInitIssue(void*)
{
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT_NO);
	inet_aton("127.0.0.1", &server_addr.sin_addr);

	long a = socket(AF_INET, SOCK_STREAM, 0);
	if (a == -1) {
		perror("[-]Socket Creation failed\n");
		exit(-1);
	}

	if (connect(a, (struct sockaddr *) &server_addr, sizeof(server_addr))  == -1) {
		perror("[-]Socket Connect failed\n");
		exit(-1);
	}
}

// initializes the server
void createServer()
{
    struct sockaddr_in s_addr;
	s_addr.sin_family = AF_INET;
	s_addr.sin_port	= htons(SERVER_PORT_NO);
	inet_aton("127.0.0.1", &s_addr.sin_addr);

	selfSock = socket(AF_INET, SOCK_STREAM, 0);
	if (selfSock == -1) {
		perror("[-]Socket Creation failed.\n");
		exit(-1);
	}

	if (bind(selfSock, (struct sockaddr*) &s_addr, sizeof(s_addr)) == -1) {
		perror("[-]Bind failed on socket.\n");
		exit(-1);	
	}

    int backlog = CLIENT_LIMIT;
	if (listen(selfSock, backlog) == -1) {
		perror("[-]Listen Failed on server:\n");
		exit(-1);
	}

    cout << "[+]Server running." << endl;
}

// handles all requests from a client
void* handleClient(void * id)
{
    short clientID = (long)id;
    char buffer[100];
    string msg;

    // tell the new client its own address (code: 0)
    bzero(buffer, 100);
    msg = "0\n" + to_string(clientID) + "\n" + clientAddr[clientID];
    strcpy(buffer, msg.c_str());
    send(clientSock[clientID], &buffer, strlen(buffer), 0);
    usleep(1000);
    
    // send new client address to all available clients (code: 1)
    for (auto i = clientSock.begin(); i != clientSock.end(); i++)
    {
        if (i->first == clientID) continue;

        bzero(buffer, 100);
        msg = "1\n" + to_string(clientID) + "\n" + clientAddr[clientID];
        strcpy(buffer, msg.c_str());
        send(i->second, &buffer, strlen(buffer), 0);
        usleep(1000);
    }

    while (1)
    {
        bzero(buffer, 100);
        recv(clientSock[clientID], buffer, 100, 0);
        msg = buffer;

        // cout << "Message recieved from " << clientAddr[clientID] << ":" << endl << msg << endl;

        // if msg is empty, client has disconnected. 
        // Remove all entries for this client.
        if (msg.empty())
        {
            cout << "[-]Disconnected from " << clientAddr[clientID] << endl;
            clientAddr.erase(clientID);
            clientSock.erase(clientID);
            clientThread.erase(clientID);
            pthread_cancel(pthread_self());
        }

        switch (msg[0])
        {
        default:
            break;
        }
    }
}

// thread to accept clients
void* acceptClients(void* b)
{
    while(1)
    {
        // printAddrs();
        cout << "[+]Awaiting connections..." << endl;

        long connfd;
	    struct sockaddr_in cliaddr;
	    socklen_t cliaddr_len;
	    connfd = accept(selfSock, (struct sockaddr*) &cliaddr, &cliaddr_len);

        if (connfd <= 0) {
            perror("[-]Accept failed on socket: ");
        }

        char ip[15];
        inet_ntop(AF_INET, &(cliaddr.sin_addr), ip, INET_ADDRSTRLEN);
        string clientName = string(ip) + ":" + to_string(cliaddr.sin_port);

        // ignore if this is the first connect
        if (clientName == "0.0.0.0:0") {continue;}
        
        cout <<  "[+]Connected to " << clientName << endl;

        // save client details in corresponding maps
        clientSock[clientCount] = connfd;
        clientAddr[clientCount] = clientName;
        clientThread[clientCount];

        // create communication thread for the newly connected client
        pthread_create(&clientThread[clientCount], NULL, handleClient, (void*)clientCount);
        clientCount++; 
    }
}

int main()
{
    createServer();
    pthread_t acceptClientThread;
    pthread_create(&acceptClientThread, NULL, acceptClients, NULL);
    usleep(100);

    pthread_t handleIssue;
    pthread_create(&handleIssue, NULL, handleInitIssue, NULL);
    initIssueHandled = true;

    pthread_join(acceptClientThread, NULL);
    
    cout << "never" << endl;
}