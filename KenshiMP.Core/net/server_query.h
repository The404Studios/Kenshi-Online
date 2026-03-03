#pragma once
#include "kmp/types.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
#include <enet/enet.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace kmp {

struct ServerQueryResult {
    std::string address;
    uint16_t    port = 0;
    std::string serverName;
    uint8_t     currentPlayers = 0;
    uint8_t     maxPlayers = 0;
    uint32_t    ping = 0;
    bool        online = false;
    bool        pending = true; // Still waiting for response
};

// Lightweight async server query.
// Uses a separate ENetHost to query servers without interfering with the game connection.
// Call QueryServer() to start a query, then PumpResults() each frame to process responses.
class ServerQueryClient {
public:
    bool Initialize();
    void Shutdown();

    // Start querying a server directly. Non-blocking.
    void QueryServer(const std::string& address, uint16_t port);

    // Query the master server for all registered game servers. Non-blocking.
    void QueryMasterServer(const std::string& masterAddress, uint16_t masterPort);

    // Pump ENet events and process responses. Call each frame.
    void Update();

    // Get current results (thread-safe copy).
    std::vector<ServerQueryResult> GetResults();

    // Clear all results and pending queries.
    void Clear();

    bool IsQueryActive() const { return m_active; }

private:
    struct PendingQuery {
        ENetPeer*   peer = nullptr;
        std::string address;
        uint16_t    port = 0;
        float       startTime = 0.f;
        bool        isMasterQuery = false; // True if this is a master server query
    };

    ENetHost* m_host = nullptr;
    bool m_initialized = false;
    bool m_active = false;

    std::vector<PendingQuery> m_pending;
    std::vector<ServerQueryResult> m_results;
    std::mutex m_mutex;
    float m_elapsed = 0.f;
};

} // namespace kmp
