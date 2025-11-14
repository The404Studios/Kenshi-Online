#include "MultiplayerSyncManager.h"
#include "IPCClient.h"
#include <sstream>
#include <cmath>

namespace ReKenshi {
namespace Multiplayer {

MultiplayerSyncManager::MultiplayerSyncManager()
    : m_initialized(false)
    , m_connected(false)
    , m_ipcClient(nullptr)
    , m_eventManager(nullptr)
    , m_localPlayerPtr(0)
    , m_syncFlags(SyncFlags::All)
    , m_syncInterval(0.1f)  // 10 Hz default
    , m_syncAccumulator(0.0f)
{
    std::memset(&m_stats, 0, sizeof(m_stats));
}

MultiplayerSyncManager::~MultiplayerSyncManager() {
    Shutdown();
}

void MultiplayerSyncManager::Initialize(
    IPC::IPCClient* ipcClient,
    Events::GameEventManager* eventManager,
    uintptr_t playerPtr)
{
    if (m_initialized) {
        return;
    }

    m_ipcClient = ipcClient;
    m_eventManager = eventManager;
    m_localPlayerPtr = playerPtr;

    // Subscribe to game events
    if (m_eventManager) {
        m_eventManager->Subscribe(Events::GameEventType::CharacterDamaged,
            [this](const Events::GameEvent& evt) { OnCharacterDamaged(evt); });

        m_eventManager->Subscribe(Events::GameEventType::CharacterMoved,
            [this](const Events::GameEvent& evt) { OnCharacterMoved(evt); });

        m_eventManager->Subscribe(Events::GameEventType::CharacterDied,
            [this](const Events::GameEvent& evt) { OnCharacterDied(evt); });

        m_eventManager->Subscribe(Events::GameEventType::PlayerConnected,
            [this](const Events::GameEvent& evt) { OnPlayerConnected(evt); });

        m_eventManager->Subscribe(Events::GameEventType::PlayerDisconnected,
            [this](const Events::GameEvent& evt) { OnPlayerDisconnected(evt); });
    }

    // Subscribe to IPC messages
    if (m_ipcClient) {
        m_ipcClient->SetMessageCallback([this](const IPC::Message& msg) {
            OnIPCMessage(msg);
        });

        m_connected = m_ipcClient->IsConnected();
    }

    m_initialized = true;
    OutputDebugStringA("[MultiplayerSync] Initialized\n");
}

void MultiplayerSyncManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_networkPlayers.clear();
    m_initialized = false;
    m_connected = false;

    OutputDebugStringA("[MultiplayerSync] Shutdown\n");
}

void MultiplayerSyncManager::Update(float deltaTime) {
    if (!m_initialized || !m_connected) {
        return;
    }

    m_syncAccumulator += deltaTime;

    // Throttle synchronization
    if (m_syncAccumulator < m_syncInterval) {
        return;
    }

    m_syncAccumulator = 0.0f;

    // Sync local player to network
    SyncLocalPlayer();

    // Sync network players to local game
    SyncNetworkPlayers();
}

void MultiplayerSyncManager::SetLocalPlayer(const std::string& playerId, const std::string& playerName) {
    m_localPlayer.playerId = playerId;
    m_localPlayer.playerName = playerName;
    m_localPlayer.characterAddress = m_localPlayerPtr;
    m_localPlayer.isLocal = true;

    std::ostringstream log;
    log << "[MultiplayerSync] Local player set: " << playerName << " (" << playerId << ")\n";
    OutputDebugStringA(log.str().c_str());
}

void MultiplayerSyncManager::AddNetworkPlayer(const std::string& playerId, const std::string& playerName) {
    NetworkPlayer player;
    player.playerId = playerId;
    player.playerName = playerName;
    player.isLocal = false;

    m_networkPlayers[playerId] = player;

    std::ostringstream log;
    log << "[MultiplayerSync] Network player added: " << playerName << " (" << playerId << ")\n";
    OutputDebugStringA(log.str().c_str());
}

void MultiplayerSyncManager::RemoveNetworkPlayer(const std::string& playerId) {
    auto it = m_networkPlayers.find(playerId);
    if (it != m_networkPlayers.end()) {
        std::ostringstream log;
        log << "[MultiplayerSync] Network player removed: " << it->second.playerName << "\n";
        OutputDebugStringA(log.str().c_str());

        m_networkPlayers.erase(it);
    }
}

void MultiplayerSyncManager::ResetStats() {
    std::memset(&m_stats, 0, sizeof(m_stats));
}

//=============================================================================
// Event Handlers
//=============================================================================

void MultiplayerSyncManager::OnCharacterDamaged(const Events::GameEvent& evt) {
    const auto& damageEvt = static_cast<const Events::CharacterDamageEvent&>(evt);

    // Only sync if it's the local player
    if (damageEvt.characterAddress != m_localPlayer.characterAddress) {
        return;
    }

    // Send damage event to network
    std::ostringstream json;
    json << "{"
         << "\"playerId\":\"" << m_localPlayer.playerId << "\","
         << "\"damage\":" << damageEvt.damageAmount << ","
         << "\"health\":" << damageEvt.healthAfter << ","
         << "\"maxHealth\":" << damageEvt.characterData.maxHealth
         << "}";

    auto msg = std::make_unique<IPC::Message>(
        IPC::MessageType::PLAYER_UPDATE,
        json.str()
    );

    m_ipcClient->SendAsync(std::move(msg));
    m_stats.packetsSent++;
    m_stats.updatesSent++;
}

void MultiplayerSyncManager::OnCharacterMoved(const Events::GameEvent& evt) {
    const auto& moveEvt = static_cast<const Events::CharacterMovementEvent&>(evt);

    // Only sync if it's the local player and movement is significant
    if (moveEvt.characterAddress != m_localPlayer.characterAddress) {
        return;
    }

    if (moveEvt.distance < POSITION_THRESHOLD) {
        return;
    }

    // Send position update to network
    SendPlayerUpdate(moveEvt.characterData);
}

void MultiplayerSyncManager::OnCharacterDied(const Events::GameEvent& evt) {
    const auto& deathEvt = static_cast<const Events::CharacterEvent&>(evt);

    // Only sync if it's the local player
    if (deathEvt.characterAddress != m_localPlayer.characterAddress) {
        return;
    }

    // Send death event to network
    std::ostringstream json;
    json << "{"
         << "\"playerId\":\"" << m_localPlayer.playerId << "\","
         << "\"event\":\"death\","
         << "\"position\":{\"x\":" << deathEvt.characterData.position.x
         << ",\"y\":" << deathEvt.characterData.position.y
         << ",\"z\":" << deathEvt.characterData.position.z << "}"
         << "}";

    auto msg = std::make_unique<IPC::Message>(
        IPC::MessageType::PLAYER_UPDATE,
        json.str()
    );

    m_ipcClient->SendAsync(std::move(msg));
    m_stats.packetsSent++;
}

void MultiplayerSyncManager::OnPlayerConnected(const Events::GameEvent& evt) {
    // Handle new player connection
    OutputDebugStringA("[MultiplayerSync] Player connected event\n");
}

void MultiplayerSyncManager::OnPlayerDisconnected(const Events::GameEvent& evt) {
    // Handle player disconnection
    OutputDebugStringA("[MultiplayerSync] Player disconnected event\n");
}

//=============================================================================
// IPC Handlers
//=============================================================================

void MultiplayerSyncManager::OnIPCMessage(const IPC::Message& msg) {
    switch (msg.GetType()) {
    case IPC::MessageType::PLAYER_UPDATE_BROADCAST:
        HandlePlayerUpdate(msg);
        break;

    case IPC::MessageType::GAME_STATE_UPDATE:
        HandleWorldUpdate(msg);
        break;

    case IPC::MessageType::CHAT_MESSAGE_BROADCAST:
        // Handle chat messages
        break;

    default:
        break;
    }

    m_stats.packetsReceived++;
}

void MultiplayerSyncManager::HandlePlayerUpdate(const IPC::Message& msg) {
    // Parse player update from IPC message
    std::string payload = msg.GetPayloadAsString();

    // TODO: Parse JSON to extract player ID and character data
    // For now, this is a simplified stub
    m_stats.updatesReceived++;

    OutputDebugStringA("[MultiplayerSync] Received player update\n");
}

void MultiplayerSyncManager::HandleWorldUpdate(const IPC::Message& msg) {
    // Handle world state updates
    OutputDebugStringA("[MultiplayerSync] Received world update\n");
}

void MultiplayerSyncManager::HandleCombatEvent(const IPC::Message& msg) {
    // Handle combat events from other players
    OutputDebugStringA("[MultiplayerSync] Received combat event\n");
}

//=============================================================================
// Sync Operations
//=============================================================================

void MultiplayerSyncManager::SyncLocalPlayer() {
    if (!m_localPlayerPtr) {
        return;
    }

    // Read current player data
    uintptr_t playerCharacter = 0;
    if (!Memory::MemoryScanner::ReadMemory(m_localPlayerPtr, playerCharacter) || !playerCharacter) {
        return;
    }

    Kenshi::CharacterData currentData;
    if (!Kenshi::GameDataReader::ReadCharacter(playerCharacter, currentData)) {
        return;
    }

    // Check if we should sync
    if (!ShouldSyncData(currentData, m_localPlayer.lastSyncedData)) {
        return;
    }

    // Send update to network
    SendPlayerUpdate(currentData);

    // Update cache
    m_localPlayer.lastSyncedData = currentData;
    m_localPlayer.lastUpdateTime = Events::GameEvent::GetCurrentTimestamp();
}

void MultiplayerSyncManager::SyncNetworkPlayers() {
    // Network player updates are handled via IPC callbacks
    // This function could apply interpolation or prediction
}

void MultiplayerSyncManager::SendPlayerUpdate(const Kenshi::CharacterData& data) {
    std::ostringstream json;
    json << "{"
         << "\"playerId\":\"" << m_localPlayer.playerId << "\","
         << "\"name\":\"" << data.name << "\",";

    if (m_syncFlags & SyncFlags::Position) {
        json << "\"position\":{\"x\":" << data.position.x
             << ",\"y\":" << data.position.y
             << ",\"z\":" << data.position.z << "},";
    }

    if (m_syncFlags & SyncFlags::Rotation) {
        json << "\"rotation\":{\"x\":" << data.rotation.x
             << ",\"y\":" << data.rotation.y
             << ",\"z\":" << data.rotation.z
             << ",\"w\":" << data.rotation.w << "},";
    }

    if (m_syncFlags & SyncFlags::Health) {
        json << "\"health\":" << data.health << ","
             << "\"maxHealth\":" << data.maxHealth << ",";
    }

    json << "\"alive\":" << (data.isAlive ? "true" : "false") << ","
         << "\"unconscious\":" << (data.isUnconscious ? "true" : "false")
         << "}";

    auto msg = std::make_unique<IPC::Message>(
        IPC::MessageType::PLAYER_UPDATE,
        json.str()
    );

    m_ipcClient->SendAsync(std::move(msg));

    m_stats.packetsSent++;
    m_stats.bytesSent += msg->Serialize().size();
    m_stats.updatesSent++;
}

void MultiplayerSyncManager::ApplyPlayerUpdate(const std::string& playerId, const Kenshi::CharacterData& data) {
    auto it = m_networkPlayers.find(playerId);
    if (it == m_networkPlayers.end()) {
        return;
    }

    NetworkPlayer& player = it->second;

    // TODO: Apply the update to the character in the game world
    // This would involve writing to the character's memory address
    // For safety, we should validate the data first

    if (player.characterAddress) {
        // Write position
        if (m_syncFlags & SyncFlags::Position) {
            Kenshi::GameDataReader::WriteCharacterPosition(player.characterAddress, data.position);
        }

        // Write health
        if (m_syncFlags & SyncFlags::Health) {
            Kenshi::GameDataReader::WriteCharacterHealth(player.characterAddress, data.health);
        }

        // Update cache
        player.lastSyncedData = data;
        player.lastUpdateTime = Events::GameEvent::GetCurrentTimestamp();
    }
}

bool MultiplayerSyncManager::ShouldSyncData(const Kenshi::CharacterData& current, const Kenshi::CharacterData& last) {
    // Check position threshold
    if (m_syncFlags & SyncFlags::Position) {
        float distance = CalculateDistance(current.position, last.position);
        if (distance > POSITION_THRESHOLD) {
            return true;
        }
    }

    // Check health threshold
    if (m_syncFlags & SyncFlags::Health) {
        float healthDiff = abs(current.health - last.health);
        if (healthDiff > HEALTH_THRESHOLD) {
            return true;
        }
    }

    // Check alive/dead state
    if (current.isAlive != last.isAlive) {
        return true;
    }

    // Check unconscious state
    if (current.isUnconscious != last.isUnconscious) {
        return true;
    }

    return false;
}

float MultiplayerSyncManager::CalculateDistance(const Kenshi::Vector3& a, const Kenshi::Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

} // namespace Multiplayer
} // namespace ReKenshi
