#include "server.h"
#include <spdlog/spdlog.h>
#include <random>
#include <algorithm>
#include <cmath>

namespace kmp {

// Kenshi-approximated combat resolution for the dedicated server.
// This implements a simplified version of Kenshi's combat model:
//
// Kenshi combat basics:
// - Attacks target specific body parts (head, chest, stomach, arms, legs)
// - Damage is split into cut and blunt (with optional pierce)
// - Defense reduces damage: effective_dmg = attack * (1 - defense/100)
// - KO threshold: any body part below -50 to -100 triggers knockout
// - Death threshold: chest or head <= -100 (permanent death in Kenshi is rare,
//   but for MP we treat critical KO as death)
// - Body part selection is weighted (chest most likely, head rare)

struct CombatResult {
    BodyPart hitPart;
    float cutDamage;
    float bluntDamage;
    float pierceDamage;
    float resultHealth;
    bool wasBlocked;
    bool wasKO;
    bool wasDeath;
};

static std::mt19937 s_rng(std::random_device{}());

// Body part selection weights (Kenshi-approximated)
static BodyPart SelectBodyPart() {
    static const struct { BodyPart part; int weight; } weights[] = {
        {BodyPart::Chest,    30},
        {BodyPart::Stomach,  20},
        {BodyPart::Head,     10},
        {BodyPart::LeftArm,  10},
        {BodyPart::RightArm, 10},
        {BodyPart::LeftLeg,  10},
        {BodyPart::RightLeg, 10},
    };
    static const int totalWeight = 100;

    std::uniform_int_distribution<int> dist(1, totalWeight);
    int roll = dist(s_rng);

    int cumulative = 0;
    for (auto& w : weights) {
        cumulative += w.weight;
        if (roll <= cumulative) return w.part;
    }
    return BodyPart::Chest;
}

// Resolve a combat attack between two entities
CombatResult ResolveCombat(const ServerEntity& attacker, ServerEntity& target,
                           uint8_t attackType) {
    CombatResult result{};
    result.hitPart = SelectBodyPart();

    // Get attacker's effective attack stat
    // In Kenshi, attack stat comes from the melee attack skill + weapon damage
    // For the server, we approximate with a base stat stored on the entity
    float attackStat = 20.f; // Default base attack
    float defenseStat = 10.f; // Default base defense

    // Weapon type determines cut/blunt split
    // 0 = balanced (50/50), 1 = cutting, 2 = blunt
    float cutRatio = 0.5f;
    float bluntRatio = 0.5f;

    // Damage formula: base = attackStat * randomFactor * (1 - defense/100)
    std::uniform_real_distribution<float> damageDist(0.8f, 1.2f);
    float randomFactor = damageDist(s_rng);
    float defenseReduction = 1.f - std::clamp(defenseStat / 100.f, 0.f, 0.9f);

    float totalDamage = attackStat * randomFactor * defenseReduction;

    result.cutDamage = totalDamage * cutRatio;
    result.bluntDamage = totalDamage * bluntRatio;
    result.pierceDamage = 0.f;

    // Block chance (simplified): 20% base block
    std::uniform_real_distribution<float> blockDist(0.f, 1.f);
    if (blockDist(s_rng) < 0.2f) {
        result.wasBlocked = true;
        result.cutDamage *= 0.3f;   // Blocked hits do 30% damage
        result.bluntDamage *= 0.3f;
    }

    // Apply damage to target health
    int partIdx = static_cast<int>(result.hitPart);
    if (partIdx >= 0 && partIdx < 7) {
        target.health[partIdx] -= (result.cutDamage + result.bluntDamage + result.pierceDamage);
        result.resultHealth = target.health[partIdx];
    }

    // Check KO threshold: any part below -50
    result.wasKO = false;
    for (int i = 0; i < 7; i++) {
        if (target.health[i] <= -50.f) {
            result.wasKO = true;
            break;
        }
    }

    // Check death threshold: chest or head <= -100
    result.wasDeath = false;
    if (target.health[static_cast<int>(BodyPart::Chest)] <= -100.f ||
        target.health[static_cast<int>(BodyPart::Head)] <= -100.f) {
        result.wasDeath = true;
        target.alive = false;
    }

    return result;
}

} // namespace kmp
