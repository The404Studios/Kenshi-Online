#include "command_registry.h"
#include <sstream>
#include <spdlog/spdlog.h>

namespace kmp {

CommandRegistry& CommandRegistry::Get() {
    static CommandRegistry instance;
    return instance;
}

void CommandRegistry::Register(const std::string& name, const std::string& desc,
                               std::function<std::string(const CommandArgs&)> handler) {
    std::lock_guard lock(m_mutex);
    CommandDef def;
    def.name = name;
    def.description = desc;
    def.handler = std::move(handler);
    m_commands[name] = std::move(def);
    spdlog::debug("CommandRegistry: Registered /{}", name);
}

std::string CommandRegistry::Execute(const std::string& input) {
    if (input.empty() || input[0] != '/') return "";

    // Parse: skip '/', split into tokens
    CommandArgs args;
    args.raw = input;

    std::istringstream iss(input.substr(1)); // Skip '/'
    std::string token;

    if (!(iss >> args.command)) return "Empty command.";

    while (iss >> token) {
        args.args.push_back(token);
    }

    // Lookup
    std::lock_guard lock(m_mutex);
    auto it = m_commands.find(args.command);
    if (it == m_commands.end()) {
        return "Unknown command: /" + args.command + ". Type /help for commands.";
    }

    // Execute handler
    try {
        return it->second.handler(args);
    } catch (const std::exception& e) {
        spdlog::error("CommandRegistry: Exception in /{}: {}", args.command, e.what());
        return "Command error: " + std::string(e.what());
    }
}

std::vector<const CommandDef*> CommandRegistry::GetAll() const {
    std::lock_guard lock(m_mutex);
    std::vector<const CommandDef*> result;
    result.reserve(m_commands.size());
    for (auto& [name, def] : m_commands) {
        result.push_back(&def);
    }
    return result;
}

} // namespace kmp
