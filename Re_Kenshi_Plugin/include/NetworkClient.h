#pragma once

#include "NetworkProtocol.h"
#include "Logger.h"
#include <windows.h>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

namespace KenshiOnline {
namespace Network {

using namespace ReKenshi::Logging;

//=============================================================================
// Network Client - IPC communication with client service
//=============================================================================

class NetworkClient {
public:
    static NetworkClient& GetInstance() {
        static NetworkClient instance;
        return instance;
    }

    // Initialize and connect to client service
    bool Connect(const std::string& pipeName = "KenshiOnline_IPC") {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_connected) {
            LOG_WARN("Already connected to client service");
            return true;
        }

        std::string fullPipeName = "\\\\.\\pipe\\" + pipeName;
        LOG_INFO_F("Connecting to client service: %s", fullPipeName.c_str());

        // Try to connect to pipe
        m_pipe = CreateFileA(
            fullPipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (m_pipe == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            LOG_ERROR_F("Failed to connect to pipe: %d", error);
            return false;
        }

        // Set pipe to message mode
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr);

        m_connected = true;
        m_running = true;

        // Start receive thread
        m_receiveThread = std::thread(&NetworkClient::ReceiveLoop, this);

        LOG_INFO("Connected to client service successfully");
        return true;
    }

    // Disconnect from client service
    void Disconnect() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_connected) {
            return;
        }

        LOG_INFO("Disconnecting from client service...");

        m_running = false;
        m_connected = false;

        if (m_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }

        if (m_receiveThread.joinable()) {
            m_receiveThread.join();
        }

        LOG_INFO("Disconnected from client service");
    }

    // Check if connected
    bool IsConnected() const {
        return m_connected;
    }

    // Send message
    bool Send(const NetworkMessage& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_connected || m_pipe == INVALID_HANDLE_VALUE) {
            return false;
        }

        std::string json = msg.Serialize() + "\n";
        DWORD bytesWritten;

        bool success = WriteFile(
            m_pipe,
            json.c_str(),
            static_cast<DWORD>(json.length()),
            &bytesWritten,
            nullptr
        );

        if (!success) {
            LOG_ERROR("Failed to send message");
            return false;
        }

        return true;
    }

    // Register message callback
    void RegisterCallback(const std::string& messageType, ProtocolHandler::MessageCallback callback) {
        m_protocolHandler.RegisterCallback(messageType, callback);
    }

    // Get queued messages
    std::vector<NetworkMessage> GetMessages() {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        std::vector<NetworkMessage> messages;
        while (!m_messageQueue.empty()) {
            messages.push_back(m_messageQueue.front());
            m_messageQueue.pop();
        }

        return messages;
    }

private:
    NetworkClient() = default;
    ~NetworkClient() {
        Disconnect();
    }
    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    // Receive loop (runs in separate thread)
    void ReceiveLoop() {
        char buffer[8192];

        while (m_running && m_connected) {
            DWORD bytesRead = 0;

            bool success = ReadFile(
                m_pipe,
                buffer,
                sizeof(buffer) - 1,
                &bytesRead,
                nullptr
            );

            if (!success || bytesRead == 0) {
                if (m_running) {
                    LOG_ERROR("Connection to client service lost");
                    m_connected = false;
                }
                break;
            }

            buffer[bytesRead] = '\0';
            std::string data(buffer, bytesRead);

            // Process received data (may contain multiple messages)
            ProcessReceivedData(data);
        }
    }

    // Process received data
    void ProcessReceivedData(const std::string& data) {
        // Split by newlines (messages are line-delimited)
        size_t start = 0;
        size_t end = data.find('\n');

        while (end != std::string::npos) {
            std::string line = data.substr(start, end - start);

            if (!line.empty()) {
                try {
                    auto msg = NetworkMessage::Deserialize(line);

                    // Add to queue
                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_messageQueue.push(msg);
                    }

                    // Call callback
                    m_protocolHandler.ProcessMessage(line);
                }
                catch (...) {
                    LOG_ERROR_F("Failed to parse message: %s", line.c_str());
                }
            }

            start = end + 1;
            end = data.find('\n', start);
        }
    }

    std::mutex m_mutex;
    std::mutex m_queueMutex;
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_running{false};
    std::thread m_receiveThread;
    ProtocolHandler m_protocolHandler;
    std::queue<NetworkMessage> m_messageQueue;
};

} // namespace Network
} // namespace KenshiOnline
