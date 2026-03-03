#pragma once
#include <cstdint>
#include <string>

namespace kmp {

// UPnP auto port mapping using Windows built-in COM API.
// No external dependencies — uses IUPnPNAT / IStaticPortMappingCollection.

class UPnPMapper {
public:
    // Attempt to map externalPort -> localIP:internalPort via UPnP.
    // protocol: "UDP" or "TCP"
    // Returns true on success.
    bool AddMapping(uint16_t externalPort, uint16_t internalPort,
                    const std::string& protocol, const std::string& description);

    // Remove a previously added mapping.
    bool RemoveMapping(uint16_t externalPort, const std::string& protocol);

    // Get the external (WAN) IP address from the router, if available.
    std::string GetExternalIP();

    // Get local LAN IP by creating a dummy UDP socket.
    static std::string GetLocalIP();

    bool IsMapped() const { return m_mapped; }

private:
    bool m_mapped = false;
    uint16_t m_mappedPort = 0;
    std::string m_mappedProtocol;
};

} // namespace kmp
