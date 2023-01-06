#include <string.h>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <pthread.h>
#include <unistd.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace fs = std::filesystem;
using namespace std;

#define SERVER_PORT_NO 30000
long serverSock; // socket for communication with server
struct sockaddr_in server_addr; // server address
long selfSock; // listening socket for this client
string selfAddr; // IP address and port number of this client

map<short, string> peerAddr; // IP addresses and port numbers of connected peers
map<short, long> peerSock; // sockets for communication with connected peers
map<short, pthread_t> peerThread; // thread handles for connected peers
short peerCount = 0;

string fileDir; // directory of files of this peer
vector<string> files; // name of files present in this peer's directory
vector<pair<short, string>> peersWithFile; // addresses of peers that have a certain file
string downloadingFileName, uploadingFileName; // name of the file being downloaded
ofstream downloadingFile, uploadingFile; // the file being downloaded

bool chatting = false; // bool to check if the peer is chatting
bool menuDone = true; // bool to check if the menu function has completed its course
pthread_t chatThread, menuThread, serverThread;

// parses message recieved from peers or servers
void parseMsg(string *arr, string &msg, int a)
{
    if (arr != NULL)
    {
        for (int i = 0; i < a; i++)
        {
            while (msg[0] != '\n')
            {
                arr[i] += msg[0];
                msg.erase(msg.begin());
            }
            msg.erase(msg.begin());
        }
        return;
    }
    for (int i = 0; i < a; i++)
    {
        while (msg[0] != '\n')
        {
            msg.erase(msg.begin());
        }
        msg.erase(msg.begin());
    }
}

// returns port number from a given address
string portFromAddr(string &addr)
{
    string port;
    int lim = addr.find(':');
    for (int i = lim + 1; i < addr.size(); i++)
    {
        port.push_back(addr[i]);
    }
    return port;
}

// search for a file within this peer's address
bool fileSearch(string filename)
{
    if (find(files.begin(), files.end(), filename) != files.end())
    {
        return true;
    }
    
    return false;
}

// handles file requests from connected peers
// sends yes to requesting peer if the file exists in directory
void handleFileReq(string msg, short id)
{
    // string temp[1];
    parseMsg(NULL, msg, 1);

    bool found = fileSearch(msg);
    
    if (!found) {return;}
    
    uploadingFileName = msg;
    char buffer[] = "2\nyes";
    send(peerSock[id], &buffer, strlen(buffer), 0);
    usleep(100);
}

// returns the size of a given file in bytes
long fileSize(string filename)
{
    ifstream file(filename, ios::binary | ios::ate);
    return file.tellg();
}

// handles the upload of a file
void handleUpload(string filePath, short peerID)
{
    string endMsg = "4\n#\n"; // message to indicate that all packets have been sent
    string buf;
    ifstream f(filePath, ios::binary);

    long size = fileSize(filePath);
    short packetNum = 0;
    int packetLen = 10000;

    while (size > 0)
    {
        if (size < packetLen) {packetLen = size;}
        
        buf = "4\n" + to_string(packetNum) + '\n';
        char ch;
        for (int i = 0; i < packetLen; i++)
        {
            f.get(ch);
            buf.push_back(ch);
        }
        send(peerSock[peerID], buf.c_str(), buf.length(), 0);
        size -= packetLen;
        packetNum++;
        usleep(10);
    }

    send(peerSock[peerID], endMsg.c_str(), endMsg.length(), 0);
    usleep(100);
    cout << "[+]File uploaded successfully." << endl;
}

// handles the download requests from a given peer
void handleDownloadReq(string msg, short peerID)
{
    cout << "[+]Download request received." << endl;
    parseMsg(NULL, msg, 1);
    string filePath = fileDir + "/" + msg;

    ifstream file(filePath, ios::binary);
    string reply = "4\n";

    if (file.fail())
    {
        reply += "-\n";
        cout << "[-]Error in reading the file." << endl;
        send(peerSock[peerID], reply.c_str(), strlen(reply.c_str()), 0);
        return;
    }

    reply += "+\n" + msg;
    send(peerSock[peerID], reply.c_str(), reply.length(), 0);
    usleep(100);

    handleUpload(filePath, peerID);
}

// handles the download of a file
void handleDownload(string msg, short peerID)
{
    string temp[2];
    parseMsg(temp, msg, 2);

    switch (temp[1][0])
    {
    case '+':
        msg = fileDir + '/' + msg;
        downloadingFile.open(msg, ios::binary);
        break;
    case '-':
        cout << "[-]Error at the peer side." << endl;
        break;
    case '#':
        downloadingFile.close();
        cout << "[+]File downloaded successfully." << endl;
        break;
    default:
        downloadingFile.write(msg.c_str(), msg.length());
        break;
    }
}

// sends a download request to the selected peer
void sendDownloadReq(short peerID, string filename)
{
    filename = "3\n" + filename;
    send(peerSock[peerID], filename.c_str(), filename.length(), 0);
    usleep(100);

    cout << "[+]Download Request sent." << endl;
}

// handles all incoming messages from a peer
void handleIncomingMsg(string msg, short peerID)
{
    // string temp[1];
    parseMsg(NULL, msg, 1);
    if (msg == "/end")
    {
        cout << "[+]" << peerAddr[peerID] << " has ended the chat Enter '/end' to end the chat." << endl;
        pthread_cancel(chatThread);
        chatting = false;
        menuDone = true;
    }
    else if (msg == "/download")
    {
        handleDownloadReq("5\n" + uploadingFileName, peerID);
    }
    else if (!msg.empty())
    {
        cout << '\n' << peerAddr[peerID] << ": " << msg << endl;
    }
}

// handles all outgoing messages from a peer
void* handleOutgoingMsg(void* id)
{
    // cout << downloadingFileName << endl;
    string msg;
    short peerID = (long)id;
    getline(cin, msg);
    fflush(stdin);
    cout << "\nYou: " << msg << endl;
    while (msg != "/end")
    {
        msg = "6\n" + msg;
        send(peerSock[peerID], msg.c_str(), msg.length(), 0);
        usleep(10);
        getline(cin, msg);
        fflush(stdin);
        cout << "You: " << msg << endl;
        usleep(100);
    }
    chatting = false;
    menuDone = true;
}

// handles chat requests from a peer
void handleChatReq(string msg, short peerID)
{
    chatting = true;
    parseMsg(NULL, msg, 1);
    string choice;

    if (msg == "no")
    {
        cout << "[+]" << peerAddr[peerID] << " rejected the chat request.\nDo you wish to download the file? [Y/n]: ";
        cin >> choice;
        fflush(stdin);

        if (choice == "Y" || choice == "y") {sendDownloadReq(peerID, downloadingFileName);}
        else if (choice == "N" || choice == "n") {}
        else {cout << "Invalid choice." << endl;}
        chatting = false;
        menuDone = true;
        return;
    }
    else if (msg == "yes")
    {
        cout << "[+]Initializing chat with " << peerAddr[peerID] << endl;
        cout << "Enter '/end' to end the chat.\nEnter '/download' to start download." << endl;
        pthread_create(&chatThread, NULL, handleOutgoingMsg, (void*)peerID);
        return;
    }

    cout << peerAddr[peerID] << " sent a chat request. Accept? [Y/n]: ";
    while (!(cin >> choice))
    {
        cin.clear();
        cin.ignore();
        cout << "Could you repeat that? [Y/n]: ";
    }

    fflush(stdin);
    string reply;

    if (choice == "Y" || choice == "y")
    {
        cout << "[+]Initializing chat with " << peerAddr[peerID] << endl;
        cout << "Enter '/end' to end the chat." << endl;
        reply = "5\nyes";
        send(peerSock[peerID], reply.c_str(), strlen(reply.c_str()), 0);
        usleep(10);
        pthread_create(&chatThread, NULL, handleOutgoingMsg, (void*)peerID);
        chatting = true;
    }
    else if (choice == "N" || choice == "n")
    {
        reply = "5\nno";
        send(peerSock[peerID], reply.c_str(), strlen(reply.c_str()), 0);
        usleep(10);
        chatting = false;
        menuDone = true;
    }
    else
    {
        cout << "[-]Invalid choice." << endl;
        chatting = false;
        menuDone = true;
    }
}

// handles all requests from a peer
void* handlePeer(void * id)
{
    char buffer[100];
    short peerID = (long)id;
    string msg;

    while (1)
    {
        bzero(buffer, 100);
        recv(peerSock[peerID], buffer, 100, 0);
        msg = buffer;

        // cout << "Message received from " << peerAddr[peerID] << ":" << endl << msg << endl;
        
        // if msg is empty, client has disconnected.
        // Remove all entries for this client.
        if (msg.empty())
        {
            cout << "[-]Disconnected from " << peerAddr[peerID] << endl;
            peerAddr.erase(peerID);
            peerSock.erase(peerID);
            peerThread.erase(peerID);
            pthread_cancel(pthread_self());
        }

        switch (msg[0])
        {
        case '1':
            handleFileReq(msg, peerID);
            break;
        
        case '2':
            peersWithFile.push_back(pair<short, string>(peerID, peerAddr[peerID]));
            break;
        
        case '3':
            handleDownloadReq(msg, peerID);
            break;

        case '4':
            handleDownload(msg, peerID);
            break;
        
        case '5':
            handleChatReq(msg, peerID);
            break;
        
        case '6':
            handleIncomingMsg(msg, peerID);
            break;

        default:
            break;
        }
    }
}

// thread to accept peers trying to connect
void* acceptPeers(void *)
{
    while (1)
    {
        struct sockaddr_in peer_addr;
        socklen_t peeraddr_len;
        long connfd = accept(selfSock, (struct sockaddr*) &peer_addr, &peeraddr_len);
        
        if (connfd <= 0) {
            perror("[-]Accept failed on socket: ");
        }

        char* ip = new char[15];
        inet_ntop(AF_INET, &(peer_addr.sin_addr), ip, INET_ADDRSTRLEN);
        string peerName = string(ip) + ":" + to_string(peer_addr.sin_port);
        delete ip;

        if (peerName == "0.0.0.0:0") {continue;}

        cout <<  "[+]Connected to " << peerName << endl;

        peerAddr[peerCount] = peerName;
        peerSock[peerCount] = connfd;
        peerThread[peerCount];
        pthread_create(&peerThread[peerCount], NULL, handlePeer, (void*)peerCount);
        peerCount++;
    }
}

// initializes the server side of this peer
void initServer(string msg)
{
    // string temp[2];
    parseMsg(NULL, msg, 2);
    selfAddr = msg;

    int portNum = stoi(portFromAddr(selfAddr));

    struct sockaddr_in self_addr;
	self_addr.sin_family = AF_INET;
	self_addr.sin_port	= htons(portNum);
	inet_aton("127.0.0.1", &self_addr.sin_addr);


	selfSock = socket(AF_INET, SOCK_STREAM, 0);
	if (selfSock == -1) {
		perror("[-]Socket Creation failed.\n");
		exit(-1);
	}

    if (bind(selfSock, (struct sockaddr*) &self_addr, sizeof(self_addr)) == -1) {
		perror("[-]Bind failed on socket.\n");
		exit(-1);	
	}

    int backlog = 10;
	if (listen(selfSock, backlog) == -1) {
		perror("[-]Listen Failed on server:\n");
		exit(-1);
	}

    cout << "[+]Client in listening mode." << endl;

    pthread_t acceptThread;
    pthread_create(&acceptThread, NULL, acceptPeers, NULL);
}

// connect to the peer with given address
void connectToPeer(string addr)
{
    string temp[2];
    parseMsg(temp, addr, 2);

    if (addr == "0.0.0.0:0") {return;}
    
    short peerID = stoi(temp[1]);
    int lim = addr.find(':');
    char ip[lim + 1] = {'\0'}, port[addr.length() - lim - 1];
    for (int i = 0; i < lim; i++)
    {
        ip[i] = addr[i];
    }
    for (int i = lim + 1; i < addr.size(); i++)
    {
        port[i - (lim + 1)] = addr[i];
    }
    
    int portNum = stoi(port);
    struct sockaddr_in peer_addr;
	peer_addr.sin_family = AF_INET;
	peer_addr.sin_port	= htons(portNum);
	inet_aton(ip, &peer_addr.sin_addr);
	long connfd = socket(AF_INET, SOCK_STREAM, 0);
	if (connfd == -1) {
		perror("[-]Socket Creation failed\n");
		exit(-1);
	}

    usleep(1000);

	if (connect(connfd, (struct sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
		perror("[-]Socket Connect failed\n");
		exit(-1);
	}

    peerSock[peerID] = connfd;
    peerAddr[peerID] = addr;

    cout << "[+]Connected to " << addr << endl;
    pthread_create(&peerThread[peerID], NULL, handlePeer, (void*)peerID);
}

// handles the communication with server
void* handleServer(void *)
{
    char buffer[100];

    while (1)
    {
        bzero(buffer, 100);
        recv(serverSock, buffer, 100, 0);
        string msg = buffer;

        // cout << "Message recieved from server: " << msg << endl;

        if (msg.empty())
        {
            cout << "[-]Disconnected from server. Exiting..." << endl;
            exit(0);
        }

        switch (msg[0])
        {
        case '0':
            initServer(msg);
        case '1':
            connectToPeer(msg);
            break;
        default:
            break;
        }
    }
}

// sends a file request to all the peers
void sendFileReq(string filename)
{
    for (auto it = peerSock.begin(); it != peerSock.end(); it++)
    {
        send(it->second, filename.c_str(), filename.length(), 0);
        usleep(100);
    }
}

// sends a chat request to a peer
void sendChatReq(short peerID)
{
    string msg = "5\nchat?";
    send(peerSock[peerID], msg.c_str(), msg.length(), 0);
    usleep(10);
    chatting = true;
}

// menu for downloading
void downloadMenu(short id)
{
    char choice;
    cout << "Do you wish to download the file? [Y/n]: ";
    cin >> choice;
    fflush(stdin);

    if (choice == 'Y' || choice == 'y')
    {
        sendDownloadReq(id, downloadingFileName);
    }
    else if (choice == 'N' || choice == 'n')
    {
        return;
    }
    else
    {
        cout << "Invalid choice." << endl;
    }
}

// menu for searching
void searchMenu()
{
    downloadingFileName = "";
    cout << "Enter a the name of the file you want (Enter -1 to go back): ";
    cin >> downloadingFileName;
    fflush(stdin);

    if (downloadingFileName == "-1") {return;}

    peersWithFile.clear();
    sendFileReq("1\n" + downloadingFileName);
    usleep(2000);

    if (peersWithFile.empty())
    {
        cout << "File not found anywhere." << endl;
        return;
    }

    cout << "File found at: " << endl;
    int a = 0;
    for (auto it = peersWithFile.begin(); it != peersWithFile.end(); it++)
    {
        cout << it->first << ' ' << it->second << endl;
    }

    short id;
    cout << "Select a peer by entering its ID (Enter -1 to go back): ";
    cin >> id;
    fflush(stdin);

    if (id == -1) {return;}

    if (peerSock.find(id) == peerSock.end())
    {
        cout << "Invalid peer ID." << endl;
        return;
    }

    char choice;
    cout << "Do you wish to chat to the peer? [Y/n]: ";
    cin >> choice;
    fflush(stdin);

    if (choice == 'Y' || choice == 'y')
    {
        sendChatReq(id);
    }
    else if (choice == 'N' || choice == 'n')
    {
        downloadMenu(id);
    }
    else
    {
        cout << "Invalid choice." << endl;
    }
    peersWithFile.clear();
}

// main menu
void* menu(void*)
{
    int choice;
    cout << "1. Search for a file." << endl;
    cout << "2. Exit." << endl;
    
    fflush(stdin);

    usleep(10);
    cin >> choice;
    
    if (cin.fail())
    {
        cin.clear();
        cin.ignore();
        cin >> choice;
    }

    switch (choice)
    {
    case 1:
        searchMenu();
        break;
    case 2:
        cout << "[+]Exiting..." << endl;
        exit(0);
        break;
    default:
        break;
    }

    // menu function has returned
    menuDone = true;
}

// connects to the server
void connectToServer()
{
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT_NO);
	inet_aton("127.0.0.1", &server_addr.sin_addr);

	serverSock = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSock == -1) {
		perror("[-]Socket Creation failed\n");
		exit(-1);
	}

	if (connect(serverSock, (struct sockaddr *) &server_addr, sizeof(server_addr))  == -1) {
		perror("[-]Socket Connect failed\n");
		exit(-1);
	}

    cout << "[+]Connected to server." << endl;
}

// gets and saves the name of files within a directory
void getFileNames(string dir)
{
    for (const auto & entry : fs::directory_iterator(fileDir))
    {
        string filepath = entry.path();
        string filename = "";
        for (int i = fileDir.length() + 1; i < filepath.length(); i++)
        {
            filename += filepath[i];
        }
        files.push_back(filename);
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        cout << "[-]Argument missing." << endl;
        exit(0);
    }
    fileDir = argv[1];


    getFileNames(fileDir);
    connectToServer();
    pthread_create(&serverThread, NULL, handleServer, NULL);
    
    while (1)
    {
        if (!chatting && menuDone)
        {
            // if the client is not chatting and the menu function has returned
            menuDone = false;
            pthread_create(&menuThread, NULL, menu, NULL);
        }
        else if (chatting)
        {
            // if client is chatting
            pthread_cancel(menuThread);
        }
        usleep(100);
    }
}