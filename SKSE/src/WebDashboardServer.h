#pragma once

/**
 * Web Dashboard Server
 *
 * Serves the IntelEngine dashboard to a web browser via:
 * - HTTP static file server (cpp-httplib) for the React SPA
 * - WebSocket server (ixwebsocket) for real-time state push/pull
 *
 * Both transports are additive — PrismaUI in-game overlay continues
 * working independently. The WS server reuses the same action dispatch
 * callbacks that PrismaUI uses (via DashboardUIManager::DispatchAction).
 *
 * Enabled via settings.yaml:
 *   web_dashboard:
 *     enabled: true
 *     http_port: 9875
 *     ws_port: 9876
 */

#include "Plugin.h"
#include <string>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>

namespace httplib { class Server; }
namespace ix { class WebSocketServer; class WebSocket; }

namespace IntelEngine {

    class WebDashboardServer {
    public:
        static WebDashboardServer* GetSingleton() {
            static WebDashboardServer instance;
            return &instance;
        }

        void Initialize();
        void Shutdown();

        // Broadcast state to all connected WebSocket clients
        void BroadcastFullState(const std::string& json);
        void BroadcastSlotData(const std::string& json);

        bool IsRunning() const { return running_.load(); }
        bool HasConnectedClients() const;

    private:
        WebDashboardServer();
        ~WebDashboardServer();
        WebDashboardServer(const WebDashboardServer&) = delete;
        WebDashboardServer& operator=(const WebDashboardServer&) = delete;

        void StartHttpServer();
        void StartWsServer();
        void BroadcastMessage(const std::string& envelope);

        // Config
        bool enabled_ = false;
        int httpPort_ = 9875;
        int wsPort_ = 9876;

        // HTTP server
        std::unique_ptr<httplib::Server> httpServer_;
        std::thread httpThread_;

        // WebSocket server
        std::unique_ptr<ix::WebSocketServer> wsServer_;

        std::atomic<bool> running_{false};

        // Last known state for new-client catch-up
        std::mutex lastStateMutex_;
        std::string lastFullState_;
    };

}  // namespace IntelEngine
