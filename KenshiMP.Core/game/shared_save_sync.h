#pragma once
#include "kmp/types.h"
#include <string>

namespace kmp::shared_save_sync {

// Initialize after connection + faction assignment.
// Determines own character name and other player's character name from faction.
void Init();

// Reset on disconnect.
void Reset();

// Called every game tick from Core::OnGameTick.
// - Discovers characters by name via char_tracker_hooks
// - Reads own position, sends to server
// - Receives other player position, writes to their character
// - Syncs game speed
void Update(float deltaTime);

// Called when we receive a position update from the server for the other player.
void OnRemotePositionReceived(const Vec3& pos);

// Called when we receive a game speed update from the server.
void OnRemoteGameSpeedReceived(float speed);

// Status queries for HUD/diagnostics
bool IsOwnCharacterFound();
bool IsOtherCharacterFound();
const std::string& GetOwnCharacterName();
const std::string& GetOtherCharacterName();

} // namespace kmp::shared_save_sync
