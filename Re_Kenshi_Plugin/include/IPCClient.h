#pragma once

#include "MessageProtocol.h"
#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>

namespace ReKenshi {
namespace IPC {

/**
 * Named Pipe IPC Client for communicating with C# backend
 */
class IPCClient {
public:
    using MessageCallback = std::function<void(const Message&)>;

    IPCClient();
    ~IPCClient();

    // Connection management
    bool Connect(const std::string& pipeName = "\\\\.\\pipe\\ReKenshi_IPC");
    void Disconnect();
    bool IsConnected() const { return m_connected; }

    // Messaging
    bool Send(const Message& message);
    bool SendAsync(std::unique_ptr<Message> message);

    // Message callbacks
    void SetMessageCallback(MessageCallback callback) { m_messageCallback = callback; }

    // Update loop (call from main thread)
    void Update();

private:
    // Background thread for reading
    void ReadThreadProc();
    void WriteThreadProc();

    // Helpers
    bool ReadMessage(Message& outMessage);
    bool WriteMessage(const Message& message);

    HANDLE m_pipeHandle;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_shouldStop;

    // Threading
    std::thread m_readThread;
    std::thread m_writeThread;

    // Message queues
    std::queue<std::unique_ptr<Message>> m_sendQueue;
    std::mutex m_sendMutex;

    std::queue<std::unique_ptr<Message>> m_receiveQueue;
    std::mutex m_receiveMutex;

    // Callbacks
    MessageCallback m_messageCallback;

    // Sequence counter
    std::atomic<uint32_t> m_sequenceCounter;
};

} // namespace IPC
} // namespace ReKenshi
