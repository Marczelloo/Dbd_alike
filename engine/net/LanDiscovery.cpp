#include "engine/net/LanDiscovery.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
using SocketLength = int;
using NativeSocket = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketLength = socklen_t;
using NativeSocket = int;
#endif

namespace engine::net
{
namespace
{
constexpr double kClientScanIntervalSeconds = 1.0;
constexpr double kServerBroadcastIntervalSeconds = 1.0;
constexpr double kServerTtlSeconds = 3.5;

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

#ifdef _WIN32
void CloseNativeSocket(int socketFd)
{
    if (socketFd >= 0)
    {
        closesocket(static_cast<SOCKET>(socketFd));
    }
}
#else
void CloseNativeSocket(int socketFd)
{
    if (socketFd >= 0)
    {
        close(socketFd);
    }
}
#endif
} // namespace

LanDiscovery::LanDiscovery() = default;

LanDiscovery::~LanDiscovery()
{
    Stop();
}

bool LanDiscovery::StartHost(
    std::uint16_t discoveryPort,
    std::uint16_t gamePort,
    const std::string& hostName,
    const std::string& mapName,
    int players,
    int maxPlayers,
    int protocolVersion,
    const std::string& buildId,
    const std::string& preferredIp
)
{
    Stop();

    m_mode = Mode::Host;
    m_discoveryPort = discoveryPort;
    m_gamePort = gamePort;
    m_hostName = hostName;
    m_mapName = mapName;
    m_players = players;
    m_maxPlayers = maxPlayers;
    m_protocolVersion = protocolVersion;
    m_buildId = buildId;
    m_preferredIp = preferredIp;

    if (!OpenSocket(m_discoveryPort, true))
    {
        m_mode = Mode::Disabled;
        return false;
    }

    m_lastHostBroadcastSeconds = 0.0;
    return true;
}

bool LanDiscovery::StartClient(std::uint16_t discoveryPort, int protocolVersion, const std::string& buildId)
{
    Stop();

    m_mode = Mode::Client;
    m_discoveryPort = discoveryPort;
    m_protocolVersion = protocolVersion;
    m_buildId = buildId;

    if (!OpenSocket(0, true))
    {
        m_mode = Mode::Disabled;
        return false;
    }

    m_lastRequestSentSeconds = 0.0;
    m_lastResponseReceivedSeconds = 0.0;
    m_servers.clear();
    return true;
}

void LanDiscovery::UpdateHostInfo(
    const std::string& mapName,
    int players,
    int maxPlayers,
    const std::string& preferredIp
)
{
    if (m_mode != Mode::Host)
    {
        return;
    }

    m_mapName = mapName;
    m_players = players;
    m_maxPlayers = maxPlayers;
    if (!preferredIp.empty())
    {
        m_preferredIp = preferredIp;
    }
}

void LanDiscovery::Stop()
{
    CloseSocket();
    m_mode = Mode::Disabled;
    m_servers.clear();
    m_lastRequestSentSeconds = 0.0;
    m_lastResponseReceivedSeconds = 0.0;
    m_lastHostBroadcastSeconds = 0.0;
}

void LanDiscovery::Tick(double nowSeconds)
{
    if (m_socket < 0)
    {
        return;
    }

    if (m_mode == Mode::Host)
    {
        TickHost(nowSeconds);
    }
    else if (m_mode == Mode::Client)
    {
        TickClient(nowSeconds);
        PruneServers(nowSeconds);
    }
}

void LanDiscovery::ForceScan()
{
    if (m_mode != Mode::Client || m_socket < 0)
    {
        return;
    }

    m_lastRequestSentSeconds = 0.0;
    SendBroadcastRequest();
}

bool LanDiscovery::OpenSocket(std::uint16_t bindPort, bool enableBroadcast)
{
#ifdef _WIN32
    bool startedNow = false;
    if (!m_wsaInitialized)
    {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            return false;
        }
        m_wsaInitialized = true;
        startedNow = true;
    }
#endif

    const int socketFd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (socketFd < 0)
    {
#ifdef _WIN32
        if (startedNow)
        {
            WSACleanup();
            m_wsaInitialized = false;
        }
#endif
        return false;
    }

    int opt = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    if (enableBroadcast)
    {
        setsockopt(socketFd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&opt), sizeof(opt));
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(socketFd), FIONBIO, &mode);
#else
    const int flags = fcntl(socketFd, F_GETFL, 0);
    fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(bindPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(static_cast<NativeSocket>(socketFd), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
    {
        CloseNativeSocket(socketFd);
#ifdef _WIN32
        if (startedNow)
        {
            WSACleanup();
            m_wsaInitialized = false;
        }
#endif
        return false;
    }

    m_socket = socketFd;
    return true;
}

void LanDiscovery::CloseSocket()
{
    if (m_socket >= 0)
    {
        CloseNativeSocket(m_socket);
        m_socket = -1;
    }

#ifdef _WIN32
    if (m_wsaInitialized)
    {
        WSACleanup();
        m_wsaInitialized = false;
    }
#endif
}

void LanDiscovery::TickHost(double nowSeconds)
{
    std::array<char, 1024> buffer{};

    while (true)
    {
        sockaddr_in from{};
        SocketLength fromLength = sizeof(from);
        const int received = recvfrom(
            static_cast<NativeSocket>(m_socket),
            buffer.data(),
            static_cast<int>(buffer.size() - 1),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLength
        );

        if (received <= 0)
        {
            break;
        }

        buffer[static_cast<std::size_t>(received)] = '\0';
        const std::string payload(buffer.data());

        if (payload.rfind("DISCOVER_REQUEST", 0) == 0)
        {
            SendResponseTo(ntohl(from.sin_addr.s_addr), ntohs(from.sin_port));
            m_lastHostBroadcastSeconds = nowSeconds;
        }
    }

    if (nowSeconds - m_lastHostBroadcastSeconds >= kServerBroadcastIntervalSeconds)
    {
        SendResponseTo(INADDR_BROADCAST, m_discoveryPort);
        m_lastHostBroadcastSeconds = nowSeconds;
    }
}

void LanDiscovery::TickClient(double nowSeconds)
{
    if (nowSeconds - m_lastRequestSentSeconds >= kClientScanIntervalSeconds)
    {
        if (SendBroadcastRequest())
        {
            m_lastRequestSentSeconds = nowSeconds;
        }
    }

    std::array<char, 1024> buffer{};

    while (true)
    {
        sockaddr_in from{};
        SocketLength fromLength = sizeof(from);
        const int received = recvfrom(
            static_cast<NativeSocket>(m_socket),
            buffer.data(),
            static_cast<int>(buffer.size() - 1),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLength
        );

        if (received <= 0)
        {
            break;
        }

        buffer[static_cast<std::size_t>(received)] = '\0';
        const std::string payload(buffer.data());

        if (payload.rfind("DISCOVER_RESPONSE", 0) != 0)
        {
            continue;
        }

        char ipBuffer[64]{};
        inet_ntop(AF_INET, &from.sin_addr, ipBuffer, sizeof(ipBuffer));

        ServerEntry entry;
        auto parseIntField = [&](const std::string& key, int fallback) {
            const std::string text = ParseField(payload, key);
            if (text.empty())
            {
                return fallback;
            }
            try
            {
                return std::stoi(text);
            }
            catch (...)
            {
                return fallback;
            }
        };

        entry.hostName = ParseField(payload, "name");
        entry.ip = ParseField(payload, "ip");
        if (entry.ip.empty())
        {
            entry.ip = ipBuffer;
        }
        entry.port = static_cast<std::uint16_t>(std::max(1, parseIntField("port", 7777)));
        entry.mapName = ParseField(payload, "map");
        if (entry.mapName.empty())
        {
            entry.mapName = "main_map";
        }
        entry.players = std::max(0, parseIntField("players", 1));
        entry.maxPlayers = std::max(1, parseIntField("max", 2));
        entry.protocolVersion = parseIntField("protocol", 1);
        entry.buildId = ParseField(payload, "build");
        entry.compatible = entry.protocolVersion == m_protocolVersion && entry.buildId == m_buildId;
        entry.lastSeenSeconds = nowSeconds;

        if (entry.ip.rfind("127.", 0) == 0)
        {
            continue;
        }
        if (!m_preferredIp.empty() && entry.ip == m_preferredIp && entry.port == m_gamePort)
        {
            continue;
        }

        UpsertServer(entry);
        m_lastResponseReceivedSeconds = nowSeconds;
    }
}

bool LanDiscovery::SendBroadcastRequest()
{
    if (m_socket < 0)
    {
        return false;
    }

    const std::string payload =
        "DISCOVER_REQUEST|protocol=" + std::to_string(m_protocolVersion) +
        "|build=" + m_buildId;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(m_discoveryPort);
    address.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const int sent = sendto(
        static_cast<NativeSocket>(m_socket),
        payload.c_str(),
        static_cast<int>(payload.size()),
        0,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    );

    return sent >= 0;
}

bool LanDiscovery::SendResponseTo(std::uint32_t ipv4HostOrder, std::uint16_t portHostOrder)
{
    if (m_socket < 0)
    {
        return false;
    }

    const std::string payload =
        "DISCOVER_RESPONSE|name=" + m_hostName +
        "|ip=" + m_preferredIp +
        "|port=" + std::to_string(m_gamePort) +
        "|map=" + m_mapName +
        "|players=" + std::to_string(m_players) +
        "|max=" + std::to_string(m_maxPlayers) +
        "|protocol=" + std::to_string(m_protocolVersion) +
        "|build=" + m_buildId;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(portHostOrder);
    address.sin_addr.s_addr = htonl(ipv4HostOrder);

    if (ipv4HostOrder == INADDR_BROADCAST)
    {
        address.sin_port = htons(m_discoveryPort);
    }

    const int sent = sendto(
        static_cast<NativeSocket>(m_socket),
        payload.c_str(),
        static_cast<int>(payload.size()),
        0,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    );

    return sent >= 0;
}

void LanDiscovery::PruneServers(double nowSeconds)
{
    m_servers.erase(
        std::remove_if(m_servers.begin(), m_servers.end(), [&](const ServerEntry& entry) {
            return nowSeconds - entry.lastSeenSeconds > kServerTtlSeconds;
        }),
        m_servers.end()
    );
}

void LanDiscovery::UpsertServer(const ServerEntry& entry)
{
    for (ServerEntry& existing : m_servers)
    {
        if (existing.ip == entry.ip && existing.port == entry.port)
        {
            existing = entry;
            return;
        }
    }

    m_servers.push_back(entry);
}

std::string LanDiscovery::ParseField(const std::string& payload, const std::string& key)
{
    const std::string token = "|" + ToLower(key) + "=";
    const std::string lowerPayload = ToLower(payload);
    const std::size_t begin = lowerPayload.find(token);
    if (begin == std::string::npos)
    {
        return {};
    }

    const std::size_t valueStart = begin + token.size();
    std::size_t valueEnd = payload.find('|', valueStart);
    if (valueEnd == std::string::npos)
    {
        valueEnd = payload.size();
    }

    return payload.substr(valueStart, valueEnd - valueStart);
}
} // namespace engine::net
