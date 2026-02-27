#pragma once

namespace kmp::entity_hooks {

bool Install();
void Uninstall();

// Re-enable the CharacterCreate hook after it was suspended during loading.
// Call this when connecting to a server so new character creates are captured.
void ResumeForNetwork();

} // namespace kmp::entity_hooks
