#include "IPCClient.h"
#include <iostream>

namespace ReKenshi {
namespace IPC {

IPCClient::IPCClient()
    : m_pipeHandle(INVALID_HANDLE_VALUE)
    , m_connected(false)
    , m_shouldStop(false)
    , m_sequenceCounter(0)
{
}

IPCClient::~IPCClient() {
    Disconnect();
}

bool IPCClient::Connect(const std::string& pipeName) {
    if (m_connected) {
        return true;
    }

    // Try to connect to named pipe
    m_pipeHandle = CreateFileA(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            // Wait for pipe to become available
            if (!WaitNamedPipeA(pipeName.c_str(), 5000)) {
                return false;
            }

            // Try again
            m_pipeHandle = CreateFileA(
                pipeName.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr
            );
        }

        if (m_pipeHandle == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    // Set pipe to message mode
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(m_pipeHandle, &mode, nullptr, nullptr)) {
        CloseHandle(m_pipeHandle);
        m_pipeHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    m_connected = true;
    m_shouldStop = false;

    // Start background threads
    m_readThread = std::thread(&IPCClient::ReadThreadProc, this);
    m_writeThread = std::thread(&IPCClient::WriteThreadProc, this);

    return true;
}

void IPCClient::Disconnect() {
    if (!m_connected) {
        return;
    }

    m_shouldStop = true;
    m_connected = false;

    // Wait for threads to finish
    if (m_readThread.joinable()) {
        m_readThread.join();
    }

    if (m_writeThread.joinable()) {
        m_writeThread.join();
    }

    // Close pipe
    if (m_pipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeHandle);
        m_pipeHandle = INVALID_HANDLE_VALUE;
    }
}

bool IPCClient::Send(const Message& message) {
    return WriteMessage(message);
}

bool IPCClient::SendAsync(std::unique_ptr<Message> message) {
    if (!m_connected) {
        return false;
    }

    // Set sequence number
    message->SetSequence(m_sequenceCounter++);

    // Add to send queue
    std::lock_guard<std::mutex> lock(m_sendMutex);
    m_sendQueue.push(std::move(message));

    return true;
}

void IPCClient::Update() {
    // Process received messages on main thread
    std::unique_lock<std::mutex> lock(m_receiveMutex);

    while (!m_receiveQueue.empty()) {
        auto msg = std::move(m_receiveQueue.front());
        m_receiveQueue.pop();

        lock.unlock();

        // Call callback
        if (m_messageCallback) {
            m_messageCallback(*msg);
        }

        lock.lock();
    }
}

void IPCClient::ReadThreadProc() {
    while (!m_shouldStop && m_connected) {
        Message message;

        if (ReadMessage(message)) {
            // Add to receive queue
            std::lock_guard<std::mutex> lock(m_receiveMutex);
            m_receiveQueue.push(std::make_unique<Message>(std::move(message)));
        } else {
            // Read failed - connection lost?
            DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING && error != ERROR_MORE_DATA) {
                m_connected = false;
                break;
            }

            // Brief sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void IPCClient::WriteThreadProc() {
    while (!m_shouldStop && m_connected) {
        std::unique_lock<std::mutex> lock(m_sendMutex);

        if (m_sendQueue.empty()) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto msg = std::move(m_sendQueue.front());
        m_sendQueue.pop();
        lock.unlock();

        // Send message
        if (!WriteMessage(*msg)) {
            // Write failed - connection lost?
            m_connected = false;
            break;
        }
    }
}

bool IPCClient::ReadMessage(Message& outMessage) {
    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Read header first
    MessageHeader header;
    DWORD bytesRead = 0;

    if (!ReadFile(m_pipeHandle, &header, sizeof(MessageHeader), &bytesRead, nullptr)) {
        return false;
    }

    if (bytesRead != sizeof(MessageHeader)) {
        return false;
    }

    // Read payload
    std::vector<uint8_t> payload;

    if (header.length > 0) {
        payload.resize(header.length);

        if (!ReadFile(m_pipeHandle, payload.data(), header.length, &bytesRead, nullptr)) {
            return false;
        }

        if (bytesRead != header.length) {
            return false;
        }
    }

    // Construct message
    std::vector<uint8_t> fullMessage;
    fullMessage.reserve(sizeof(MessageHeader) + payload.size());

    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    fullMessage.insert(fullMessage.end(), headerBytes, headerBytes + sizeof(MessageHeader));
    fullMessage.insert(fullMessage.end(), payload.begin(), payload.end());

    auto msg = Message::Deserialize(fullMessage);
    if (!msg) {
        return false;
    }

    outMessage = std::move(*msg);
    return true;
}

bool IPCClient::WriteMessage(const Message& message) {
    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    auto data = message.Serialize();
    DWORD bytesWritten = 0;

    if (!WriteFile(m_pipeHandle, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr)) {
        return false;
    }

    return bytesWritten == data.size();
}

} // namespace IPC
} // namespace ReKenshi
