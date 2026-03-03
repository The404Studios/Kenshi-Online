#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kmp {

struct CommandArgs {
    std::string              raw;     // Full input (e.g., "/tp Bob")
    std::string              command; // First token without / (e.g., "tp")
    std::vector<std::string> args;    // Remaining tokens (e.g., ["Bob"])
};

struct CommandDef {
    std::string name;
    std::string description;
    std::function<std::string(const CommandArgs&)> handler;
};

class CommandRegistry {
public:
    static CommandRegistry& Get();

    void Register(const std::string& name, const std::string& desc,
                  std::function<std::string(const CommandArgs&)> handler);

    // Parse "/" prefix, lookup command, call handler, return result string.
    // Returns empty string if input doesn't start with '/'.
    std::string Execute(const std::string& input);

    // Get all registered commands (for /help listing)
    std::vector<const CommandDef*> GetAll() const;

    // Register all built-in commands (called once during Core::Initialize)
    void RegisterBuiltins();

private:
    CommandRegistry() = default;

    mutable std::recursive_mutex m_mutex;
    std::unordered_map<std::string, CommandDef> m_commands;
};

} // namespace kmp
