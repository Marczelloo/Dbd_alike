#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace engine::net
{
class NetworkSession
{
public:
    enum class Mode
    {
        Offline,
        Host,
        Client
    };

    struct PollEvent
    {
        bool connected = false;
        bool disconnected = false;
        std::vector<std::uint8_t> payload;
    };

    struct ConnectionStats
    {
        bool available = false;
        std::uint32_t rttMs = 0;
        std::uint32_t packetLoss = 0;
        std::uint32_t peerCount = 0;
    };

    NetworkSession() = default;
    ~NetworkSession();

    NetworkSession(const NetworkSession&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;

    bool Initialize();
    void Shutdown();

    bool StartHost(std::uint16_t port, std::size_t maxPeers = 1);
    bool StartClient(const std::string& host, std::uint16_t port);
    void Disconnect();

    void Poll(int timeoutMs = 0);
    [[nodiscard]] std::optional<PollEvent> PopEvent();

    bool SendReliable(const void* data, std::size_t size);
    [[nodiscard]] ConnectionStats GetConnectionStats() const;

    [[nodiscard]] Mode GetMode() const { return m_mode; }
    [[nodiscard]] bool IsConnected() const { return m_connectedPeer != nullptr && m_connected; }
    [[nodiscard]] bool HasActiveConnection() const { return m_connected; }

private:
    bool EnsureInitialized();
    void ResetTransport();

    bool m_initialized = false;
    bool m_connected = false;
    Mode m_mode = Mode::Offline;

    _ENetHost* m_host = nullptr;
    _ENetPeer* m_connectedPeer = nullptr;

    std::vector<PollEvent> m_events;
};
} // namespace engine::net
