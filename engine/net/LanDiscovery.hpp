#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::net
{
class LanDiscovery
{
public:
    struct ServerEntry
    {
        std::string hostName;
        std::string ip;
        std::uint16_t port = 7777;
        std::string mapName = "main_map";
        int players = 1;
        int maxPlayers = 2;
        std::string buildId;
        int protocolVersion = 1;
        bool compatible = true;
        double lastSeenSeconds = 0.0;
    };

    enum class Mode
    {
        Disabled,
        Host,
        Client
    };

    LanDiscovery();
    ~LanDiscovery();

    LanDiscovery(const LanDiscovery&) = delete;
    LanDiscovery& operator=(const LanDiscovery&) = delete;

    bool StartHost(
        std::uint16_t discoveryPort,
        std::uint16_t gamePort,
        const std::string& hostName,
        const std::string& mapName,
        int players,
        int maxPlayers,
        int protocolVersion,
        const std::string& buildId,
        const std::string& preferredIp
    );

    bool StartClient(std::uint16_t discoveryPort, int protocolVersion, const std::string& buildId);
    void Stop();

    void UpdateHostInfo(
        const std::string& mapName,
        int players,
        int maxPlayers,
        const std::string& preferredIp
    );

    void Tick(double nowSeconds);
    void ForceScan();

    void SetDebugEnabled(bool enabled) { m_debugEnabled = enabled; }
    [[nodiscard]] bool DebugEnabled() const { return m_debugEnabled; }

    [[nodiscard]] Mode GetMode() const { return m_mode; }
    [[nodiscard]] bool IsRunning() const { return m_socket >= 0; }
    [[nodiscard]] std::uint16_t DiscoveryPort() const { return m_discoveryPort; }

    [[nodiscard]] const std::vector<ServerEntry>& Servers() const { return m_servers; }

    [[nodiscard]] double LastRequestSentSeconds() const { return m_lastRequestSentSeconds; }
    [[nodiscard]] double LastResponseReceivedSeconds() const { return m_lastResponseReceivedSeconds; }
    [[nodiscard]] double LastHostBroadcastSeconds() const { return m_lastHostBroadcastSeconds; }

private:
    bool OpenSocket(std::uint16_t bindPort, bool enableBroadcast);
    void CloseSocket();

    void TickHost(double nowSeconds);
    void TickClient(double nowSeconds);

    bool SendBroadcastRequest();
    bool SendResponseTo(std::uint32_t ipv4HostOrder, std::uint16_t portHostOrder);

    void PruneServers(double nowSeconds);
    void UpsertServer(const ServerEntry& entry);

    static std::string ParseField(const std::string& payload, const std::string& key);

    int m_socket = -1;
    Mode m_mode = Mode::Disabled;
    bool m_debugEnabled = false;
#ifdef _WIN32
    bool m_wsaInitialized = false;
#endif

    std::uint16_t m_discoveryPort = 7778;
    std::uint16_t m_gamePort = 7777;

    std::string m_hostName;
    std::string m_mapName = "main_map";
    int m_players = 1;
    int m_maxPlayers = 2;
    int m_protocolVersion = 1;
    std::string m_buildId = "dev";
    std::string m_preferredIp;

    double m_lastRequestSentSeconds = 0.0;
    double m_lastResponseReceivedSeconds = 0.0;
    double m_lastHostBroadcastSeconds = 0.0;

    std::vector<ServerEntry> m_servers;
};
} // namespace engine::net
