#include "server.h"
#include <nlohmann/json.hpp>
#include <fstream>
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

        entityArray.push_back(e);
    }
    j["entities"] = entityArray;

    std::ofstream file(path);
    if (!file.is_open()) {
        spdlog::error("SaveWorld: Failed to open '{}'", path);
        return false;
    }
    file << j.dump(2);
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

        for (auto& e : j["entities"]) {
            ServerEntity entity;
            entity.id = e["id"];
            entity.type = static_cast<EntityType>(e["type"].get<int>());
            entity.owner = e["owner"];
            entity.templateId = e["templateId"];
            entity.factionId = e["factionId"];

            auto& pos = e["position"];
            entity.position = Vec3(pos[0], pos[1], pos[2]);

            auto& rot = e["rotation"];
            entity.rotation = Quat(rot[0], rot[1], rot[2], rot[3]);

            entity.zone = ZoneCoord::FromWorldPos(entity.position);
            entity.alive = e.value("alive", true);

            auto& health = e["health"];
            for (int i = 0; i < 7 && i < static_cast<int>(health.size()); i++) {
                entity.health[i] = health[i];
            }

            entities[entity.id] = entity;
            if (entity.id > maxId) maxId = entity.id;
        }

        nextEntityId = maxId + 1;
        spdlog::info("LoadWorld: Loaded {} entities from '{}'", entities.size(), path);
        return true;
    } catch (const std::exception& ex) {
        spdlog::error("LoadWorld: Failed to parse '{}': {}", path, ex.what());
        return false;
    }
}

} // namespace kmp
