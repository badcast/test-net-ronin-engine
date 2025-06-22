
#pragma once

#include <string>

#include <SDL/SDL_net.h>

class TCPServer;

struct ClientTransform
{
    float x,y,angle;
};

class TCPClient
{
protected:
    TCPsocket socket;

public:
    TCPClient();
    int connect(std::string host, uint16_t port, std::string userName, ClientTransform clientInfo, int *id);
    bool isConnect();
    void send(const char * data, size_t len);
    int read(char * data, size_t len);
    void close();

    static int readClientInfo(int * id, std::string *str, ClientTransform *t, const char buffer[]);

private:
    friend class TCPServer;
};

class TCPServer
{
protected:
    struct TCPData * data;

public:
    TCPServer();
    ~TCPServer();
    int listen(uint16_t port);
    void deploy(bool wait = false);
    bool isRun();
    int clients();
    void close();

private:
    friend class TCPClient;
};
