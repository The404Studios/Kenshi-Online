#pragma once
#include "kmp/types.h"
#include "kmp/constants.h"
#include <enet/enet.h>
#include <cstdint>
#include <string>
#include <functional>
#include <mutex>
#include <vector>
#include <atomic>

namespace kmp {

class NetworkClient {
public:
    using PacketCallback = std::function<void(const uint8_t* data, size_t size, int channel)>;

    bool Initialize();
    void Shutdown();

    bool Connect(const std::string& address, uint16_t port);       // Blocking (legacy)
    bool ConnectAsync(const std::string& address, uint16_t port); // Non-blocking
    void Disconnect();
    void Update(); // Pump ENet events - call frequently

    // Send on specific channels
    void SendReliable(const uint8_t* data, size_t len);
    void SendReliableUnordered(const uint8_t* data, size_t len);
    void SendUnreliable(const uint8_t* data, size_t len);

    void SetPacketCallback(PacketCallback cb) { m_callback = cb; }

    bool IsConnected() const { return m_connected; }
    bool IsConnecting() const { return m_connecting; }
    uint32_t GetPing() const;
    const std::string& GetServerAddress() const { return m_serverAddr; }
    uint16_t GetServerPort() const { return m_serverPort; }

private:
    ENetHost*   m_host       = nullptr;
    ENetPeer*   m_serverPeer = nullptr;
    PacketCallback m_callback;
    std::mutex  m_sendMutex;

    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_connecting{false};
    float       m_connectStartTime = 0.f;
    std::string m_serverAddr;
    uint16_t    m_serverPort = 0;
    bool        m_initialized = false;

    void Send(const uint8_t* data, size_t len, int channel, uint32_t flags);
};

} // namespace kmp
