#include "THeartbeatThread.h"

#include "Client.h"
#include "Http.h"
//#include "SocketIO.h"
#include <sstream>

void THeartbeatThread::operator()() {
    RegisterThread("Heartbeat");
    std::string Body;
    std::string T;

    // these are "hot-change" related variables
    static std::string Last;

    static std::chrono::high_resolution_clock::time_point LastNormalUpdateTime = std::chrono::high_resolution_clock::now();
    bool isAuth = false;
    while (!mShutdown) {
        Body = GenerateCall();
        // a hot-change occurs when a setting has changed, to update the backend of that change.
        auto Now = std::chrono::high_resolution_clock::now();
        bool Unchanged = Last == Body;
        auto TimePassed = (Now - LastNormalUpdateTime);
        auto Threshold = Unchanged ? 30 : 5;
        if (TimePassed < std::chrono::seconds(Threshold)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
#ifdef DEBUG
        debug("heartbeat @ " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(TimePassed).count()));
#endif // DEBUG

        Last = Body;
        LastNormalUpdateTime = Now;
        if (!Application::Settings.CustomIP.empty())
            Body += "&ip=" + Application::Settings.CustomIP;

        Body += "&pps=" + Application::PPS();

        T = Http::POST(Application::GetBackendHostname(), "/heartbeat", {}, Body, false);

        if (T.substr(0, 2) != "20") {
            //Backend system refused server startup!
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            T = Http::POST(Application::GetBackendHostname(), "/heartbeat", {}, Body, false);
            // TODO backup2 + HTTP flag (no TSL)
            if (T.substr(0, 2) != "20") {
                warn("Backend system refused server! Server might not show in the public list");
                debug("server returned \"" + T + "\"");
                isAuth = false;
            }
        }

        if (!isAuth) {
            if (T == "2000") {
                info(("Authenticated!"));
                isAuth = true;
            } else if (T == "200") {
                info(("Resumed authenticated session!"));
                isAuth = true;
            }
        }

        //SocketIO::Get().SetAuthenticated(isAuth);
    }
}
std::string THeartbeatThread::GenerateCall() {
    std::stringstream Ret;

    Ret << "uuid=" << Application::Settings.Key
        << "&players=" << mServer.ClientCount()
        << "&maxplayers=" << Application::Settings.MaxPlayers
        << "&port=" << Application::Settings.Port
        << "&map=" << Application::Settings.MapName
        << "&private=" << (Application::Settings.Private ? "true" : "false")
        << "&version=" << Application::ServerVersion()
        << "&clientversion=" << Application::ClientVersion()
        << "&name=" << Application::Settings.ServerName
        << "&modlist=" << mResourceManager.TrimmedList()
        << "&modstotalsize=" << mResourceManager.MaxModSize()
        << "&modstotal=" << mResourceManager.ModsLoaded()
        << "&playerslist=" << GetPlayers()
        << "&desc=" << Application::Settings.ServerDesc;
    return Ret.str();
}
THeartbeatThread::THeartbeatThread(TResourceManager& ResourceManager, TServer& Server)
    : mResourceManager(ResourceManager)
    , mServer(Server) {
    Application::RegisterShutdownHandler([&] {
        if (mThread.joinable()) {
            debug("shutting down Heartbeat");
            mShutdown = true;
            mThread.join();
            debug("shut down Heartbeat");
        }
    });
    Start();
}
std::string THeartbeatThread::GetPlayers() {
    std::string Return;
    mServer.ForEachClient([&](const std::weak_ptr<TClient>& ClientPtr) -> bool {
        ReadLock Lock(mServer.GetClientMutex());
        if (!ClientPtr.expired()) {
            Return += ClientPtr.lock()->GetName() + ";";
        }
        return true;
    });
    return Return;
}
/*THeartbeatThread::~THeartbeatThread() {
}*/
