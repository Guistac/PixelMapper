#pragma once
#include <asio.hpp>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include "shared/network/CommandProtocol.h"
#include "shared/network/TelemetryProtocol.h"

namespace PixelMapper::Network {

class AsioNetworkManager {
public:
    static AsioNetworkManager& getInstance();

    void startServer(uint16_t tcpPort, uint16_t udpPort);
    void startClient(const std::string& serverHost, uint16_t tcpPort, uint16_t udpPort);
    void stop();

    bool isServer() const { return m_isServer; }
    bool isRunning() const { return m_isRunning; }

    // Client -> Server command sending (TCP)
    void sendCommandToServer(CommandType type, const std::string& jsonPayload);

    // Server -> Clients command broadcasting (TCP)
    void broadcastCommandToClients(CommandType type, const std::string& jsonPayload);

    // Server -> One specific new client (TCP) — used for Initial Sync on connection
    void sendCommandToNewClient(std::shared_ptr<asio::ip::tcp::socket> socket, CommandType type, const std::string& jsonPayload);

    // High frequency Telemetry Sender (Server Only, UDP)
    void streamTelemetry(uint32_t frameNumber, const Generative::EngineStateUBO& ubo, const ColorRGBW* pixels, uint32_t pixelCount);

    // Set callback for receiving telemetry data (Client Only)
    void setTelemetryCallback(std::function<void(const Generative::EngineStateUBO&, const ColorRGBW*, uint32_t)> callback);

    // Set callback for commands (Both Server and Client)
    void setCommandCallback(std::function<void(CommandType, const std::string&)> callback);

    // Set callback for when a client connects (Server Only) — receives the newly connected socket
    void setClientConnectCallback(std::function<void(std::shared_ptr<asio::ip::tcp::socket>)> callback);

private:
    AsioNetworkManager();
    ~AsioNetworkManager();

    // Prevent copy/assignment
    AsioNetworkManager(const AsioNetworkManager&) = delete;
    AsioNetworkManager& operator=(const AsioNetworkManager&) = delete;

    asio::io_context m_ioContext;
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> m_workGuard;
    std::thread m_networkThread;

    bool m_isServer = false;
    bool m_isRunning = false;

    // TCP Command Pipe (Server Variables)
    std::unique_ptr<asio::ip::tcp::acceptor> m_tcpAcceptor;
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> m_clientSockets;
    std::mutex m_clientsMutex;

    // TCP Command Pipe (Client Variables)
    std::shared_ptr<asio::ip::tcp::socket> m_serverSocket;
    std::mutex m_serverSocketMutex;

    // UDP Telemetry Pipe (Server)
    std::unique_ptr<asio::ip::udp::socket> m_udpServerSendSocket;
    std::vector<asio::ip::udp::endpoint> m_clientUdpEndpoints;
    std::mutex m_endpointsMutex;

    // UDP Telemetry Pipe (Client)
    std::unique_ptr<asio::ip::udp::socket> m_udpClientRecvSocket;
    asio::ip::udp::endpoint m_udpSenderEndpoint;
    std::vector<uint8_t> m_udpRecvBuffer;

    // Callbacks
    std::function<void(const Generative::EngineStateUBO&, const ColorRGBW*, uint32_t)> m_telemetryCallback;
    std::function<void(CommandType, const std::string&)> m_commandCallback;
    std::function<void(std::shared_ptr<asio::ip::tcp::socket>)> m_clientConnectCallback;

    // Reassembly buffer for incoming UDP telemetry slices (Client side)
    struct TelemetryReassembly {
        uint32_t frameNumber = 0xFFFFFFFF;
        uint32_t totalFrameSize = 0;
        uint32_t pixelCount = 0;
        std::vector<uint8_t> buffer;
        std::vector<bool> receivedSlices;
        uint16_t receivedCount = 0;
    };
    TelemetryReassembly m_reassembly;
    std::mutex m_reassemblyMutex;

    // Internal socket helpers
    void startAccept();
    void handleClientConnection(std::shared_ptr<asio::ip::tcp::socket> socket);
    void readCommandHeader(std::shared_ptr<asio::ip::tcp::socket> socket);
    void readCommandPayload(std::shared_ptr<asio::ip::tcp::socket> socket, CommandHeader header);
    
    void startClientTcpRead();
    void readClientCommandHeader();
    void readClientCommandPayload(CommandHeader header);

    void startUdpReceive();
    void processReceivedSlice(const uint8_t* data, size_t bytesTransferred);
};

} // namespace PixelMapper::Network
