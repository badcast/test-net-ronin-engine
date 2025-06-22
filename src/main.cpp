#include <iostream>
#include <map>
#include <thread>
#include <tuple>

#include <ronin/Debug.h>
#include <ronin/framework.h>

#include <ronin/SharedPointer.h>
#include <ronin/SharedPointerEnd.h>

#include "socket_tcp.h"

using namespace std;
using namespace RoninEngine;
using namespace RoninEngine::Runtime;

constexpr auto DefaultHost = "localhost";
constexpr uint16_t DefaultPort = 8888;

class Player : public Behaviour
{
public:
    int id;
    bool isRemote;
    std::string userName;
    SpriteRendererRef renderer;

    Player();

    ~Player();

    void OnStart();

    void OnUpdate();
};

class WF : public World
{
public:
    TCPClient *client;
    std::map<int, std::tuple<Vec2,float,Ref<Player>>> players;
    bool workable;
    UI::uid butConnect;

    WF() : World("Test") {  client = new TCPClient();  }
    Ref<Player> player;

    Camera2DRef camera;

    std::thread * t;

    void OnUnloading()
    {
        workable = false;
        if(t)
        {
            t->join();
            delete t;
        }
    }

    void OnStart(){
        camera = Primitive::CreateCamera2D();
        camera->visibleGrids = true;
        camera->visibleNames = true;
        camera->visibleObjects = true;


        butConnect = GetGUI()->PushButton("Connect", {0,0, 160, 30});

        player = createPlayer();
        player->gameObject()->SetActive(true);

        workable = true;

        auto func = [this]()
        {
            while(workable)
            {
                // If client isn't connect, then wait of connection.
                if(!this->client->isConnect())
                {
                    continue;
                }

                // Sync
                constexpr int bufSize = 2048;
                ClientTransform t;
                char buffer[bufSize];
                char * pb = buffer;
                int x,y;
                std::tuple<Vec2,float,Ref<Player>> * p;

                this->client->read(pb, sizeof(y));
                y = *reinterpret_cast<int*>(pb);
                int reads = this->client->read(pb,bufSize);
                for(x=0; x < y; ++x)
                {
                    std::string _name;
                    int id = 0;
                    int bytes = TCPClient::readClientInfo(&id, &_name, &t, pb);
                    pb += bytes;
                    if(id != this->player->id)
                    {
                        auto it = this->players.find(id);
                        if(it == this->players.end())
                        {
                            // Создать peer
                            p = &this->players[id];
                            std::get<1>(*p) = id;
                            std::get<Vec2>(*p) = Vec2{t.x,t.y};
                            std::get<float>(*p) = t.angle;
                            std::get<Ref<Player>>(*p) = createPlayer();
                            std::get<Ref<Player>>(*p)->userName = _name;
                            std::get<Ref<Player>>(*p)->isRemote = true;
                        }
                        else
                        {
                            p = &std::get<1>(*it);
                            std::get<Vec2>(*p) = Vec2{t.x,t.y};
                            std::get<float>(*p) = t.angle;
                        }
                    }
                }

                // Send current transform to Server
                t.x = this->player->transform()->position().x;
                t.y = this->player->transform()->position().y;
                t.angle = this->player->transform()->angle();
                this->client->send(reinterpret_cast<char*>(&t), sizeof(t));
            }
        };

        t = new std::thread(func);
    }


    void OnUpdate()
    {
        camera->transform()->position(Vec2::Lerp(camera->transform()->position(), player->transform()->position(), Time::deltaTime() * 2));

        if(GetGUI()->ButtonClicked(butConnect))
        {
            Vec2 p = player->transform()->position();
            float a = player->transform()->angle();
            while(client->connect(DefaultHost, DefaultPort, player->userName, {p.x, p.y, a}, &player->id))
                ;

            GetGUI()->ElementSetVisible(butConnect, false);
        }

        if(!client->isConnect())
        {
            return;
        }

        //Сглаживание Transform
        for(std::pair<int, std::tuple<Vec2,float,Ref<Player>>> peer : players)
        {
            std::tuple<Vec2,float,Ref<Player>> p = std::get<1>(peer);
            Ref<Player> pl = std::get<Ref<Player>>(p);
            if(pl.isNull())
                continue;
            pl->transform()->position(Vec2::Lerp(pl->transform()->position(),std::get<Vec2>(p), Time::deltaTime() * 2));
            pl->transform()->angle(Math::LerpAngle(pl->transform()->angle(), std::get<float>(p), Time::deltaTime() * 10));
        }
    }

    static Ref<Player> createPlayer()
    {
        GameObjectRef obj = Primitive::CreateEmptyGameObject(Vec2::zero);
        GameObjectRef spr = Primitive::CreateEmptyGameObject();
        ResId img = Resources::LoadImage(Paths::GetRuntimeDir() + "/player.png", true);
        spr->AddComponent<SpriteRenderer>()->setSprite(Primitive::CreateSpriteFrom(Resources::GetImageSource(img)));
        spr->spriteRenderer()->setSize(Vec2::one * .3f);
        spr->transform()->setParent(obj->transform());
        spr->transform()->angle(-97);
        return obj->AddComponent<Player>();
    }

    void OnGizmos()
    {
        if(!client->isConnect())
        {
            RenderUtility::DrawTextToScreen(Vec2::RoundToInt(camera->WorldToScreenPoint({-.5f,0})),"Click to connect", 17);
            return;
        }

        for(std::pair<int, std::tuple<Vec2,float,Ref<Player>>> peer : players)
        {
            std::tuple<Vec2,float,Ref<Player>> p2 = std::get<1>(peer);
            Ref<Player> client = std::get<Ref<Player>>(p2);
            if(client.isNull())
                continue;
            RenderUtility::DrawText(client->transform()->position(), client->userName);
        }
    }
};

#ifdef WIN32
typedef void* HINSTANCE;
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, char* lpCmdLine, int nShowCmd)
#else
int main(int argc, char **argv)
#endif
{
    TCPServer serv;
    serv.listen(DefaultPort);
    serv.deploy();

    RoninSimulator::Init();
    RoninSimulator::SetDebugMode(true);
    RoninSimulator::Show(Resolution::GetMidResolution());
    RoninSimulator::LoadWorldAfterSplash<WF>();
    RoninSimulator::Simulate();
    RoninSimulator::Finalize();
    serv.close();
    return 0;
}

Player::Player() : Behaviour("Player Script"), userName("Default Player"), isRemote(false)
{
}

Player::~Player()
{
}

void Player::OnStart()
{
    renderer = GetComponent<SpriteRenderer>();
}

void Player::OnUpdate()
{
    if(isRemote)
        return;
    Vec2 ms = Camera::ScreenToWorldPoint(Input::GetMousePointf());
    transform()->LookAtLerp(ms, Vec2::up, Time::deltaTime() * 2);
    transform()->Translate(Input::GetAxis() * .03f);
}
