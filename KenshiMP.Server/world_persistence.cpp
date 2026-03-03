#include "server.h"
#include "kmp/constants.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <spdlog/spdlog.h>

namespace kmp {

using json = nlohmann::json;

// Save/load world state to JSON for the dedicated server.
// This allows VPS-hosted servers to persist world state between restarts.

bool SaveWorldToFile(const std::string& path,
                     const std::unordered_map<EntityID, ServerEntity>& entities,
                     float timeOfDay, int weatherState) {
    json j;
    j["version"] = 1;
    j["timeOfDay"] = timeOfDay;
    j["weather"] = weatherState;

    json entityArray = json::array();
    for (auto& [id, entity] : entities) {
        json e;
        e["id"] = entity.id;
        e["type"] = static_cast<int>(entity.type);
        e["owner"] = entity.owner;
        e["templateId"] = entity.templateId;
        e["factionId"] = entity.factionId;
        e["position"] = {entity.position.x, entity.position.y, entity.position.z};
        e["rotation"] = {entity.rotation.w, entity.rotation.x, entity.rotation.y, entity.rotation.z};
        e["alive"] = entity.alive;

        json healthArr = json::array();
        for (int i = 0; i < 7; i++) healthArr.push_back(entity.health[i]);
        e["health"] = healthArr;

        e["templateName"] = entity.templateName;

        json equipArr = json::array();
        for (int i = 0; i < 14; i++) equipArr.push_back(entity.equipment[i]);
        e["equipment"] = equipArr;

        entityArray.push_back(e);
    }
    j["entities"] = entityArray;

    // Atomic write: write to temp file, then rename (safe on NTFS)
    std::string tmpPath = path + ".tmp";
    std::ofstream file(tmpPath);
    if (!file.is_open()) {
        spdlog::error("SaveWorld: Failed to open '{}'", tmpPath);
        return false;
    }
    file << j.dump(2);
    file.close();

    // Rename over the real file (atomic on most filesystems)
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        // rename fails if destination exists on some platforms; remove first
        std::remove(path.c_str());
        if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
            spdlog::error("SaveWorld: Failed to rename '{}' to '{}'", tmpPath, path);
            return false;
        }
    }
    spdlog::info("SaveWorld: Saved {} entities to '{}'", entities.size(), path);
    return true;
}

bool LoadWorldFromFile(const std::string& path,
                       std::unordered_map<EntityID, ServerEntity>& entities,
                       float& timeOfDay, int& weatherState,
                       EntityID& nextEntityId) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;

        timeOfDay = j.value("timeOfDay", 0.5f);
        weatherState = j.value("weather", 0);

        entities.clear();
        EntityID maxId = 0;

        int loadedCount = 0;
        int skippedBadPos = 0;
        for (auto& e : j["entities"]) {
            if (loadedCount >= KMP_MAX_SYNC_ENTITIES) {
                spdlog::warn("LoadWorld: Entity cap reached ({} max), skipping remaining",
                             KMP_MAX_SYNC_ENTITIES);
                break;
            }
            ServerEntity entity;
            entity.id = e["id"];
            entity.type = static_cast<EntityType>(e["type"].get<int>());
            entity.owner = e["owner"];
            entity.templateId = e["templateId"];
            entity.factionId = e["factionId"];

            auto& pos = e["position"];
            entity.position = Vec3(pos[0], pos[1], pos[2]);

            // Validate position — skip entities with NaN, inf, or extreme coordinates
            if (std::isnan(entity.position.x) || std::isnan(entity.position.y) || std::isnan(entity.position.z) ||
                std::isinf(entity.position.x) || std::isinf(entity.position.y) || std::isinf(entity.position.z) ||
                std::abs(entity.position.x) > 1000000.f || std::abs(entity.position.y) > 1000000.f ||
                std::abs(entity.position.z) > 1000000.f) {
                skippedBadPos++;
                spdlog::warn("LoadWorld: Skipping entity {} — bad position ({:.1f}, {:.1f}, {:.1f})",
                             entity.id, entity.position.x, entity.position.y, entity.position.z);
                // Still track max ID to prevent collisions
                if (entity.id > maxId) maxId = entity.id;
                continue;
            }

            auto& rot = e["rotation"];
            entity.rotation = Quat(rot[0], rot[1], rot[2], rot[3]);

            entity.zone = ZoneCoord::FromWorldPos(entity.position);
            entity.alive = e.value("alive", true);

            auto& health = e["health"];
            for (int i = 0; i < 7 && i < static_cast<int>(health.size()); i++) {
                entity.health[i] = health[i];
            }

            entity.templateName = e.value("templateName", std::string{});

            if (e.contains("equipment")) {
                auto& equip = e["equipment"];
                for (int i = 0; i < 14 && i < static_cast<int>(equip.size()); i++) {
                    entity.equipment[i] = equip[i];
                }
            }

            entities[entity.id] = entity;
            if (entity.id > maxId) maxId = entity.id;
            loadedCount++;
        }

        nextEntityId = maxId + 1;
        if (skippedBadPos > 0) {
            spdlog::warn("LoadWorld: Skipped {} entities with bad positions", skippedBadPos);
        }
        spdlog::info("LoadWorld: Loaded {} valid entities from '{}'", entities.size(), path);
        return true;
    } catch (const std::exception& ex) {
        spdlog::error("LoadWorld: Failed to parse '{}': {}", path, ex.what());
        return false;
    }
}

} // namespace kmp
