#include "engine/net/NetworkSession.hpp"

#include <cstring>
#include <iostream>

#include <enet/enet.h>

namespace engine::net
{
NetworkSession::~NetworkSession()
{
    Shutdown();
}

bool NetworkSession::Initialize()
{
    if (m_initialized)
    {
        return true;
    }

    if (enet_initialize() != 0)
    {
        std::cerr << "ENet initialization failed.\n";
        return false;
    }

    m_initialized = true;
    return true;
}

void NetworkSession::Shutdown()
{
    Disconnect();

    if (m_initialized)
    {
        enet_deinitialize();
        m_initialized = false;
    }
}

bool NetworkSession::StartHost(std::uint16_t port, std::size_t maxPeers)
{
    if (!EnsureInitialized())
    {
        return false;
    }

    ResetTransport();

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;

    m_host = enet_host_create(&address, maxPeers, 2, 0, 0);
    if (m_host == nullptr)
    {
        std::cerr << "Failed to create ENet host.\n";
        m_mode = Mode::Offline;
        return false;
    }

    m_mode = Mode::Host;
    m_connected = false;
    return true;
}

bool NetworkSession::StartClient(const std::string& host, std::uint16_t port)
{
    if (!EnsureInitialized())
    {
        return false;
    }

    ResetTransport();

    m_host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (m_host == nullptr)
    {
        std::cerr << "Failed to create ENet client host.\n";
        m_mode = Mode::Offline;
        return false;
    }

    ENetAddress address{};
    if (enet_address_set_host(&address, host.c_str()) != 0)
    {
        std::cerr << "Failed to resolve host: " << host << "\n";
        ResetTransport();
        m_mode = Mode::Offline;
        return false;
    }
    address.port = port;

    m_connectedPeer = enet_host_connect(m_host, &address, 2, 0);
    if (m_connectedPeer == nullptr)
    {
        std::cerr << "Failed to connect ENet peer.\n";
        ResetTransport();
        m_mode = Mode::Offline;
        return false;
    }

    m_mode = Mode::Client;
    m_connected = false;
    return true;
}

void NetworkSession::Disconnect()
{
    if (m_connectedPeer != nullptr)
    {
        enet_peer_disconnect(m_connectedPeer, 0);

        ENetEvent event{};
        while (m_host != nullptr && enet_host_service(m_host, &event, 10) > 0)
        {
            if (event.type == ENET_EVENT_TYPE_RECEIVE)
            {
                enet_packet_destroy(event.packet);
            }
        }

        m_connectedPeer = nullptr;
    }

    ResetTransport();
    m_mode = Mode::Offline;
    m_connected = false;
    m_events.clear();
}

void NetworkSession::Poll(int timeoutMs)
{
    if (m_host == nullptr)
    {
        return;
    }

    ENetEvent event{};
    while (enet_host_service(m_host, &event, timeoutMs) > 0)
    {
        timeoutMs = 0;

        switch (event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                m_connectedPeer = event.peer;
                m_connected = true;
                PollEvent pollEvent;
                pollEvent.connected = true;
                m_events.push_back(std::move(pollEvent));
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                PollEvent pollEvent;
                pollEvent.payload.resize(event.packet->dataLength);
                if (event.packet->dataLength > 0)
                {
                    std::memcpy(pollEvent.payload.data(), event.packet->data, event.packet->dataLength);
                }
                m_events.push_back(std::move(pollEvent));
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                m_connected = false;
                if (event.peer == m_connectedPeer)
                {
                    m_connectedPeer = nullptr;
                }
                PollEvent pollEvent;
                pollEvent.disconnected = true;
                m_events.push_back(std::move(pollEvent));
                break;
            }
            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }
}

std::optional<NetworkSession::PollEvent> NetworkSession::PopEvent()
{
    if (m_events.empty())
    {
        return std::nullopt;
    }

    PollEvent event = std::move(m_events.front());
    m_events.erase(m_events.begin());
    return event;
}

bool NetworkSession::SendReliable(const void* data, std::size_t size)
{
    if (!m_connected || m_connectedPeer == nullptr || data == nullptr || size == 0)
    {
        return false;
    }

    ENetPacket* packet = enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr)
    {
        return false;
    }

    if (enet_peer_send(m_connectedPeer, 0, packet) != 0)
    {
        enet_packet_destroy(packet);
        return false;
    }

    enet_host_flush(m_host);
    return true;
}

bool NetworkSession::BroadcastReliable(const void* data, std::size_t size)
{
    if (m_host == nullptr || data == nullptr || size == 0)
    {
        return false;
    }

    ENetPacket* packet = enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr)
    {
        return false;
    }

    // Broadcast to all connected peers
    enet_host_broadcast(m_host, 0, packet);
    enet_host_flush(m_host);
    return true;
}

NetworkSession::ConnectionStats NetworkSession::GetConnectionStats() const
{
    ConnectionStats stats;
    if (m_host != nullptr)
    {
        stats.peerCount = static_cast<std::uint32_t>(m_host->connectedPeers);
    }

    if (m_connectedPeer != nullptr)
    {
        stats.available = true;
        stats.rttMs = m_connectedPeer->roundTripTime;
        stats.packetLoss = m_connectedPeer->packetLoss;
    }
    return stats;
}

std::size_t NetworkSession::ConnectedPeerCount() const
{
    if (m_host == nullptr)
    {
        return 0;
    }
    return m_host->connectedPeers;
}

bool NetworkSession::EnsureInitialized()
{
    return Initialize();
}

void NetworkSession::ResetTransport()
{
    if (m_host != nullptr)
    {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_connectedPeer = nullptr;
}
} // namespace engine::net
