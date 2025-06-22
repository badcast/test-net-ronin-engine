#include <vector>
#include <mutex>
#include <chrono>
#include <thread>
#include <memory>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <functional>

#include "json.hpp"
#include "socket_tcp.h"

constexpr auto MagicBytes = "RoninEngineTCP:v001";

enum
{
    ThreadPull,
    ThreadStop
};

struct ClientData
{
    int id;
    std::string name;
    struct ClientTransform transfrm;
    TCPsocket socket;

    static int fillBuffer(char buffer[255], const ClientData & d)
    {
        int i;
        char * p = buffer;

        // write id
        memcpy(p, &d.id, sizeof(d.id));
        p += sizeof(d.id);

        // write name
        i = std::min<int>(200,d.name.length());
        memcpy(p, &i, sizeof(i));
        p += sizeof(i);
        memcpy(p, d.name.c_str(), i);
        p += i;

        // write transform
        memcpy(p, &d.transfrm, sizeof(d.transfrm));
        p += sizeof(d.transfrm);
        return static_cast<int>(p - buffer);
    }
    static int fromBuffer(ClientData & d, const char buffer[])
    {
        int i;
        const char * p = buffer;

        // read id
        memcpy(&d.id, p, sizeof(d.id));
        p += sizeof(d.id);

        // read str len
        memcpy(&i, p, sizeof(i));
        p += sizeof(i);

        // read str
        i = std::max<int>(0,std::min<int>(i, 200));
        d.name.assign(p, i);
        p += i;

        // read transform
        memcpy(&d.transfrm, p, sizeof(d.transfrm));
        p += sizeof(d.transfrm);
        return static_cast<int>(p-buffer);
    }
};

struct TCPData
{
    int cmd;
    std::mutex locker;
    std::unique_ptr<std::thread> th_accept;
    std::unique_ptr<std::thread> th_deploy;
    std::map<int,ClientData> clients;
    int uniqueId;

    uint16_t assignPort;
    IPaddress addr;
    TCPsocket socket;
};


void _init()
{
    static bool initComplete = false;
    if(!initComplete && SDLNet_Init() == 0)
    {
        initComplete = true;
    }
}

std::string makeIPstr(IPaddress addr)
{
    char ipStr[16];
    snprintf(ipStr, 16, "%d.%d.%d.%d",
             (addr.host & 0xFF000000) >> 24,
             (addr.host & 0x00FF0000) >> 16,
             (addr.host & 0x0000FF00) >> 8,
             addr.host & 0x000000FF);
    return {ipStr};
}

////////////////////////////////////////////////
/// SERVER
////////////////////////////////////////////////

void TCPServerAccept(TCPData* data)
{
    using namespace std::chrono;

    constexpr int MaxBufferLen = 128;

    int received;
    int num0;
    char buffer[MaxBufferLen];
    TCPsocket client;

    while(data->cmd != ThreadStop)
    {
        client = SDLNet_TCP_Accept(data->socket);
        if(!client)
        {
            std::this_thread::sleep_for(milliseconds(100));
            continue;
        }

        received = SDLNet_TCP_Recv(client, buffer, MaxBufferLen);
        if(received <= 0)
        {
            SDLNet_TCP_Close(client);
            continue;
        }

        // Провести фильтр над клиентами, чтобы получить совместимых клиентов.
        num0 = std::strlen(MagicBytes);
        if(received < num0 || std::strncmp(MagicBytes, buffer, num0))
        {
            // Send Invalid
            SDLNet_TCP_Close(client);
            continue;
        }

        // Send byte 1 (YES)
        buffer[0] = 1;
        SDLNet_TCP_Send(client, buffer, 1);

        SDLNet_SocketSet sockSet = SDLNet_AllocSocketSet(1);
        SDLNet_TCP_AddSocket(sockSet, client);
        int activeSocks = SDLNet_CheckSockets(sockSet, 1000); // Wait 1000ms
        if(activeSocks < 1)
        {
            // Send Invalid
            SDLNet_FreeSocketSet(sockSet);
            SDLNet_TCP_Close(client);
            continue;
        }
        SDLNet_FreeSocketSet(sockSet);

        // Read client name and it's identity
        received = SDLNet_TCP_Recv(client, buffer, MaxBufferLen);
        if(received <= 0)
        {
            // Send Invalid
            SDLNet_TCP_Close(client);
            continue;
        }
        json::JSON j = json::JSON::Load(std::string(buffer, received));
        if(j.IsNull())
        {
            // Send Invalid
            SDLNet_TCP_Close(client);
            continue;
        }

        ClientData cldata;
        cldata.name = j["name"].ToString();
        cldata.transfrm.x = j["x"].ToFloat();
        cldata.transfrm.y = j["y"].ToFloat();
        cldata.transfrm.angle = j["a"].ToFloat();
        cldata.socket = client;

        data->locker.lock();
        cldata.id = ++data->uniqueId;
        data->clients[cldata.id] = cldata;

        // Send Complete and ID
        buffer[0] = 1;
        (*reinterpret_cast<int*>(buffer+1)) = cldata.id;
        SDLNet_TCP_Send(client, buffer, 5);

        std::cout << "Register new user " << cldata.name << " id "<< cldata.id << std::endl;
        data->locker.unlock();
    }
}

TCPServer::TCPServer()
{
    data = new TCPData {0};
    _init();
}

TCPServer::~TCPServer()
{
    close();
    delete data;
}

int TCPServer::listen(uint16_t port)
{
    if(isRun() || SDLNet_ResolveHost(&data->addr, nullptr, port) < 0)
        return -1;
    data->socket = SDLNet_TCP_Open(&data->addr);
    if(!data->socket)
        return -1;
    data->assignPort = port;
    data->clients = {};
    data->cmd = ThreadPull;
    data->th_accept = std::make_unique<std::thread>(TCPServerAccept, data);
    return 0;
}

void TCPServer::deploy(bool wait)
{
    if(!isRun())
    {
        std::cerr << "Server is not run" << std::endl;
        return;
    }
    std::cout << "Server runs in: " << makeIPstr(data->addr) << ":" << data->assignPort << std::endl;

    std::function<void(TCPServer*,TCPData*)> __func = [](TCPServer * server, TCPData * data){
        constexpr int bufSize = 4096;
        char buffer[bufSize] {0};
        int x,y,z;
        char * p;
        std::map<int,ClientData> localClients;
        std::vector<ClientData> changedClients;
        SDLNet_SocketSet sockets = SDLNet_AllocSocketSet(16);

        while ( data->cmd != ThreadStop )
        {
            data->locker.lock();
            localClients = data->clients;

            data->locker.unlock();

            if(!localClients.empty())
            {
                 // Prepare
                p = buffer;
                y = localClients.size();
                memcpy(p, &y, sizeof(y));
                p += sizeof(y);
                for(auto iter = std::begin(localClients); iter != std::end(localClients); ++iter)
                {
                    int bytes = ClientData::fillBuffer(p, std::get<1>(*iter));
                    p += bytes;
                }
                z = static_cast<int>(p - buffer);

                // Send Client Transforms to all clients.
                for(auto iter = std::begin(localClients); iter != std::end(localClients); ++iter)
                {
                    ClientData & cl = std::get<1>(*iter);
                    SDLNet_TCP_Send(cl.socket, buffer, z);
                }

                // Receive Client post Information
                for(auto iter = std::begin(localClients); iter != std::end(localClients); ++iter)
                {
                    ClientData & cl = std::get<1>(*iter);
                    y = SDLNet_TCP_Recv(cl.socket, &cl.transfrm, sizeof(cl.transfrm));
                    if(y <= 0)
                        continue;
                    changedClients.emplace_back(cl);
                }

                data->locker.lock();
                // Apply Changed
                for(x = 0; x < changedClients.size(); ++x)
                {
                    data->clients[changedClients[x].id] = changedClients[x];
                }
                data->locker.unlock();

                // Очистить мусор.
                changedClients.clear();
            }
            else {
                // Do wait if is not work
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        SDLNet_FreeSocketSet(sockets);

    };

    data->th_deploy = std::make_unique<std::thread >(__func, this, data);
    if(wait)
        data->th_deploy->join();
    return;
}

bool TCPServer::isRun()
{
    return data->socket != nullptr && data->th_accept != nullptr;
}

int TCPServer::clients()
{
    int count;
    data->locker.lock();
    count = static_cast<int>(data->clients.size());
    data->locker.unlock();
    return count;
}

void TCPServer::close()
{
    if(!isRun())
        return;

    data->cmd = ThreadStop;
    if(data->th_accept != nullptr)
    {
        data->th_accept->join();
        data->th_accept.reset();
    }
    if(data->th_deploy != nullptr)
    {
        data->th_deploy->join();
        data->th_deploy.reset();
    }
    data->uniqueId = 0;

    for(int x = 0; x < data->clients.size(); ++x)
        SDLNet_TCP_Close(data->clients[x].socket);
    data->clients.clear();

    SDLNet_TCP_Close(data->socket);
    data->socket = nullptr;
}

////////////////////////////////////////////////
/// CLIENT
////////////////////////////////////////////////

TCPClient::TCPClient() : socket(nullptr)
{
    _init();
}

int TCPClient::connect(std::string host, uint16_t port, std::string userName, ClientTransform clientInfo, int * id)
{
    int num0;
    IPaddress addr;
    constexpr int MaxBuffer =128;
    char buffer[MaxBuffer];

    if(id == nullptr)
        return -1;

    SDLNet_ResolveHost(&addr, host.c_str(), port);
    TCPsocket _sock = SDLNet_TCP_Open(&addr);
    if(!_sock)
        return -1;

    num0 = strlen(MagicBytes);
    SDLNet_TCP_Send(_sock, MagicBytes, num0);
    num0=SDLNet_TCP_Recv(_sock, buffer, MaxBuffer);
    if(num0 != 1 && buffer[0] != 1)
    {
        SDLNet_TCP_Close(_sock);
        return -1;
    }

    json::JSON j;
    j["name"] = userName;
    j["x"] = clientInfo.x;
    j["y"] = clientInfo.y;
    j["a"] = clientInfo.angle;

    std::string s = j.dump();
    SDLNet_TCP_Send(_sock,s.c_str(), s.length());

    num0 = SDLNet_TCP_Recv(_sock,buffer, MaxBuffer);
    if(num0 < 5 || !buffer[0])
    {
        SDLNet_TCP_Close(_sock);
        return -1;
    }

    (*id) = *(buffer+1);
    socket = _sock;
    return 0;
}

bool TCPClient::isConnect()
{
    return socket != nullptr;
}

void TCPClient::send(const char *data, size_t len)
{
    SDLNet_TCP_Send(socket, data, len);
}

int TCPClient::read(char *data, size_t len)
{
    return SDLNet_TCP_Recv(socket, data, len);
}

void TCPClient::close()
{
    if(!socket)
        return;
    SDLNet_TCP_Close(socket);
    socket = nullptr;
}

int TCPClient::readClientInfo(int *id, std::string * str, ClientTransform * t, const char buffer[])
{
    ClientData d;
    int bytes = ClientData::fromBuffer(d, buffer);
    if(id != nullptr)
        *id = d.id;
    if(str != nullptr)
        *str = d.name;
    if(t != nullptr)
        *t = d.transfrm;
    return bytes;
}
