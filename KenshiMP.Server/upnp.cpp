#include "upnp.h"
#include <spdlog/spdlog.h>

#include <Windows.h>
#include <natupnp.h>
#include <comdef.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace kmp {

std::string UPnPMapper::GetLocalIP() {
    // Create a UDP socket and "connect" to a public IP to determine local route.
    // No actual traffic is sent.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "127.0.0.1";

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return "127.0.0.1";
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);

    if (connect(sock, (sockaddr*)&target, sizeof(target)) != 0) {
        closesocket(sock);
        WSACleanup();
        return "127.0.0.1";
    }

    sockaddr_in local{};
    int localLen = sizeof(local);
    if (getsockname(sock, (sockaddr*)&local, &localLen) != 0) {
        closesocket(sock);
        WSACleanup();
        return "127.0.0.1";
    }

    char ipStr[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &local.sin_addr, ipStr, sizeof(ipStr));

    closesocket(sock);
    // Don't WSACleanup here — server needs WinSock alive for ENet
    return ipStr;
}

bool UPnPMapper::AddMapping(uint16_t externalPort, uint16_t internalPort,
                            const std::string& protocol, const std::string& description) {
    spdlog::info("UPnP: Attempting to map port {} ({})...", externalPort, protocol);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hr);
    // S_FALSE means already initialized, which is fine

    IUPnPNAT* nat = nullptr;
    hr = CoCreateInstance(__uuidof(UPnPNAT), nullptr, CLSCTX_ALL,
                          __uuidof(IUPnPNAT), (void**)&nat);
    if (FAILED(hr) || !nat) {
        spdlog::warn("UPnP: Failed to create UPnPNAT instance (hr=0x{:08X})", (unsigned)hr);
        spdlog::warn("UPnP: Router may not support UPnP. Players will need to forward port {} manually.", externalPort);
        if (needUninit) CoUninitialize();
        return false;
    }

    IStaticPortMappingCollection* mappings = nullptr;

    // The UPnP discovery can be slow — retry a few times
    for (int attempt = 0; attempt < 3; attempt++) {
        hr = nat->get_StaticPortMappingCollection(&mappings);
        if (SUCCEEDED(hr) && mappings) break;
        spdlog::debug("UPnP: Discovery attempt {} failed, retrying...", attempt + 1);
        Sleep(1000);
    }

    if (FAILED(hr) || !mappings) {
        spdlog::warn("UPnP: Could not get port mapping collection (hr=0x{:08X})", (unsigned)hr);
        spdlog::warn("UPnP: UPnP may be disabled on router. Forward port {} manually.", externalPort);
        nat->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    // Get local IP
    std::string localIP = GetLocalIP();
    spdlog::info("UPnP: Local IP is {}", localIP);

    // Convert strings to BSTR
    std::wstring wProtocol(protocol.begin(), protocol.end());
    std::wstring wLocalIP(localIP.begin(), localIP.end());
    std::wstring wDescription(description.begin(), description.end());

    BSTR bstrProtocol = SysAllocString(wProtocol.c_str());
    BSTR bstrLocalIP = SysAllocString(wLocalIP.c_str());
    BSTR bstrDescription = SysAllocString(wDescription.c_str());

    IStaticPortMapping* mapping = nullptr;
    hr = mappings->Add(externalPort, bstrProtocol, internalPort,
                       bstrLocalIP, VARIANT_TRUE, bstrDescription, &mapping);

    SysFreeString(bstrProtocol);
    SysFreeString(bstrLocalIP);
    SysFreeString(bstrDescription);

    if (SUCCEEDED(hr) && mapping) {
        // Try to get external IP from the mapping
        BSTR bstrExternalIP = nullptr;
        if (SUCCEEDED(mapping->get_ExternalIPAddress(&bstrExternalIP)) && bstrExternalIP) {
            int len = WideCharToMultiByte(CP_UTF8, 0, bstrExternalIP, -1, nullptr, 0, nullptr, nullptr);
            std::string extIP(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, bstrExternalIP, -1, extIP.data(), len, nullptr, nullptr);
            spdlog::info("UPnP: External IP is {}", extIP);
            SysFreeString(bstrExternalIP);
        }
        mapping->Release();

        m_mapped = true;
        m_mappedPort = externalPort;
        m_mappedProtocol = protocol;

        spdlog::info("UPnP: Successfully mapped port {} -> {}:{} ({})",
                     externalPort, localIP, internalPort, protocol);
    } else {
        spdlog::warn("UPnP: Failed to add port mapping (hr=0x{:08X})", (unsigned)hr);
        spdlog::warn("UPnP: Port {} may already be mapped or router denied the request.", externalPort);
    }

    mappings->Release();
    nat->Release();
    // Don't CoUninitialize — keep COM alive for potential later calls

    return m_mapped;
}

bool UPnPMapper::RemoveMapping(uint16_t externalPort, const std::string& protocol) {
    if (!m_mapped) return true; // Nothing to remove

    spdlog::info("UPnP: Removing port mapping {} ({})...", externalPort, protocol);

    IUPnPNAT* nat = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(UPnPNAT), nullptr, CLSCTX_ALL,
                                  __uuidof(IUPnPNAT), (void**)&nat);
    if (FAILED(hr) || !nat) return false;

    IStaticPortMappingCollection* mappings = nullptr;
    hr = nat->get_StaticPortMappingCollection(&mappings);
    if (FAILED(hr) || !mappings) {
        nat->Release();
        return false;
    }

    std::wstring wProtocol(protocol.begin(), protocol.end());
    BSTR bstrProtocol = SysAllocString(wProtocol.c_str());

    hr = mappings->Remove(externalPort, bstrProtocol);

    SysFreeString(bstrProtocol);
    mappings->Release();
    nat->Release();

    if (SUCCEEDED(hr)) {
        spdlog::info("UPnP: Port mapping removed");
        m_mapped = false;
        return true;
    } else {
        spdlog::warn("UPnP: Failed to remove mapping (hr=0x{:08X})", (unsigned)hr);
        return false;
    }
}

std::string UPnPMapper::GetExternalIP() {
    IUPnPNAT* nat = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(UPnPNAT), nullptr, CLSCTX_ALL,
                                  __uuidof(IUPnPNAT), (void**)&nat);
    if (FAILED(hr) || !nat) return "";

    IStaticPortMappingCollection* mappings = nullptr;
    hr = nat->get_StaticPortMappingCollection(&mappings);
    if (FAILED(hr) || !mappings) {
        nat->Release();
        return "";
    }

    // We need at least one mapping to read the external IP from
    // Try to get our own mapping
    if (m_mapped) {
        std::wstring wProtocol(m_mappedProtocol.begin(), m_mappedProtocol.end());
        BSTR bstrProtocol = SysAllocString(wProtocol.c_str());

        IStaticPortMapping* mapping = nullptr;
        hr = mappings->get_Item(m_mappedPort, bstrProtocol, &mapping);
        SysFreeString(bstrProtocol);

        if (SUCCEEDED(hr) && mapping) {
            BSTR bstrIP = nullptr;
            if (SUCCEEDED(mapping->get_ExternalIPAddress(&bstrIP)) && bstrIP) {
                int len = WideCharToMultiByte(CP_UTF8, 0, bstrIP, -1, nullptr, 0, nullptr, nullptr);
                std::string ip(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, bstrIP, -1, ip.data(), len, nullptr, nullptr);
                SysFreeString(bstrIP);
                mapping->Release();
                mappings->Release();
                nat->Release();
                return ip;
            }
            mapping->Release();
        }
    }

    mappings->Release();
    nat->Release();
    return "";
}

} // namespace kmp
