/**
 * Web Dashboard Server Implementation
 *
 * HTTP static file server + WebSocket server for browser-based dashboard.
 *
 * Data flow:
 *   Papyrus -> PushFullState(json) -> DashboardUIManager -> BroadcastFullState -> WS clients
 *   Browser -> WS message { action, ...payload } -> DispatchAction -> same C++ callbacks -> Papyrus
 */

#include "WebDashboardServer.h"
#include "DashboardConfig.h"
#include "DashboardUIManager.h"

// cpp-httplib requires Win10+ (_WIN32_WINNT >= 0x0A00) but CommonLibSSE
// sets 0x0601 (Win7) globally. Override for this TU only.
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00
#include <httplib.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace IntelEngine {

    // Constructor/destructor defined here so unique_ptr has complete types
    WebDashboardServer::WebDashboardServer() = default;
    WebDashboardServer::~WebDashboardServer() { Shutdown(); }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void WebDashboardServer::Initialize() {
        if (running_.load()) return;

        auto* config = DashboardConfig::GetSingleton();
        std::string settingsPath = config->GetSettingsPath();

        std::string enabledStr = config->ReadYamlString(settingsPath, "web_dashboard", "enabled", "false");
        enabled_ = (enabledStr == "true");
        if (!enabled_) {
            logger::info("[WebDashboard] Disabled in settings.yaml");
            return;
        }

        std::string httpPortStr = config->ReadYamlString(settingsPath, "web_dashboard", "http_port", "9875");
        std::string wsPortStr = config->ReadYamlString(settingsPath, "web_dashboard", "ws_port", "9876");
        try { httpPort_ = std::stoi(httpPortStr); } catch (...) { httpPort_ = 9875; }
        try { wsPort_ = std::stoi(wsPortStr); } catch (...) { wsPort_ = 9876; }

        ix::initNetSystem();

        StartHttpServer();
        StartWsServer();

        running_.store(true);
        logger::info("[WebDashboard] Started — HTTP on port {}, WS on port {}", httpPort_, wsPort_);
    }

    void WebDashboardServer::Shutdown() {
        if (!running_.load()) return;
        running_.store(false);

        if (wsServer_) {
            wsServer_->stop();
            wsServer_.reset();
        }

        if (httpServer_) {
            httpServer_->stop();
        }
        if (httpThread_.joinable()) {
            httpThread_.join();
        }
        httpServer_.reset();

        ix::uninitNetSystem();
        logger::info("[WebDashboard] Shut down");
    }

    // =========================================================================
    // HTTP Static File Server
    // =========================================================================

    void WebDashboardServer::StartHttpServer() {
        httpServer_ = std::make_unique<httplib::Server>();

        // Resolve web root: Data/PrismaUI/views/IntelEngine/dashboard/
        std::filesystem::path webRoot = std::filesystem::current_path()
            / "Data" / "PrismaUI" / "views" / "IntelEngine" / "dashboard";
        std::string webRootStr = webRoot.string();

        logger::info("[WebDashboard] HTTP serving from: {}", webRootStr);

        // CORS pre-routing
        httpServer_->set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            return httplib::Server::HandlerResponse::Unhandled;
        });

        // Config endpoint — tells the browser which WS port to connect to
        int wsPort = wsPort_;
        httpServer_->Get("/api/config", [wsPort](const httplib::Request&, httplib::Response& res) {
            nlohmann::json j;
            j["wsPort"] = wsPort;
            res.set_content(j.dump(), "application/json");
        });

        // Static file serving
        httpServer_->set_mount_point("/", webRootStr);

        // Start on background thread
        httpThread_ = std::thread([this]() {
            if (!httpServer_->listen("127.0.0.1", httpPort_)) {
                logger::error("[WebDashboard] HTTP server failed to bind on port {}", httpPort_);
            }
        });
    }

    // =========================================================================
    // WebSocket Server
    // =========================================================================

    void WebDashboardServer::StartWsServer() {
        wsServer_ = std::make_unique<ix::WebSocketServer>(wsPort_, "127.0.0.1");
        wsServer_->disablePerMessageDeflate();

        wsServer_->setOnClientMessageCallback(
            [this](std::shared_ptr<ix::ConnectionState> connectionState,
                   ix::WebSocket& webSocket,
                   const ix::WebSocketMessagePtr& msg)
            {
                if (msg->type == ix::WebSocketMessageType::Open) {
                    logger::info("[WebDashboard] WS client connected: {}",
                                connectionState->getRemoteIp());

                    // Send last known state for catch-up
                    std::string catchUp;
                    {
                        std::lock_guard<std::mutex> lock(lastStateMutex_);
                        catchUp = lastFullState_;
                    }
                    if (!catchUp.empty()) {
                        webSocket.send(catchUp);
                    }
                    // Request fresh full state from Papyrus
                    DashboardUIManager::GetSingleton()->RequestFullState();
                }
                else if (msg->type == ix::WebSocketMessageType::Close) {
                    logger::info("[WebDashboard] WS client disconnected: {}",
                                connectionState->getRemoteIp());
                }
                else if (msg->type == ix::WebSocketMessageType::Message) {
                    // Parse action message: { "action": "cancelTask", "slot": 0, ... }
                    try {
                        auto j = nlohmann::json::parse(msg->str);
                        std::string action = j.value("action", "");
                        if (action.empty()) return;

                        // The payload is the full JSON (action callbacks expect it)
                        DashboardUIManager::DispatchAction(action, msg->str);
                    } catch (const std::exception& e) {
                        logger::warn("[WebDashboard] Failed to parse WS message: {}", e.what());
                    }
                }
                else if (msg->type == ix::WebSocketMessageType::Error) {
                    logger::warn("[WebDashboard] WS error: {}", msg->errorInfo.reason);
                }
            });

        auto res = wsServer_->listen();
        if (!res.first) {
            logger::error("[WebDashboard] WS server failed to bind on port {}: {}",
                         wsPort_, res.second);
            wsServer_.reset();
            return;
        }

        wsServer_->start();
    }

    // =========================================================================
    // Broadcasting
    // =========================================================================

    bool WebDashboardServer::HasConnectedClients() const {
        if (!wsServer_) return false;
        return !wsServer_->getClients().empty();
    }

    void WebDashboardServer::BroadcastMessage(const std::string& envelope) {
        if (!wsServer_) return;

        for (auto& client : wsServer_->getClients()) {
            client->send(envelope);
        }
    }

    void WebDashboardServer::BroadcastFullState(const std::string& json) {
        nlohmann::json envelope;
        envelope["type"] = "fullState";
        envelope["data"] = nlohmann::json::parse(json, nullptr, false);
        std::string msg = envelope.dump();

        {
            std::lock_guard<std::mutex> lock(lastStateMutex_);
            lastFullState_ = msg;
        }

        BroadcastMessage(msg);
    }

    void WebDashboardServer::BroadcastSlotData(const std::string& json) {
        nlohmann::json envelope;
        envelope["type"] = "slots";
        envelope["data"] = nlohmann::json::parse(json, nullptr, false);
        BroadcastMessage(envelope.dump());
    }

}  // namespace IntelEngine
