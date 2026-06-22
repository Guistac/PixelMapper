#include "shared/network/AsioNetworkManager.h"
#include <iostream>

namespace PixelMapper::Network {

AsioNetworkManager::AsioNetworkManager() {
    m_workGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(m_ioContext)
    );
}

AsioNetworkManager::~AsioNetworkManager() {
    stop();
}

AsioNetworkManager& AsioNetworkManager::getInstance() {
    static AsioNetworkManager instance;
    return instance;
}

void AsioNetworkManager::startServer(uint16_t tcpPort, uint16_t udpPort) {
    if (m_isRunning) return;
    
    m_isServer = true;
    m_isRunning = true;

    try {
        // Setup TCP Acceptor
        m_tcpAcceptor = std::make_unique<asio::ip::tcp::acceptor>(
            m_ioContext, 
            asio::ip::tcp::endpoint(asio::ip::tcp::v4(), tcpPort)
        );
        m_tcpAcceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));

        // Setup UDP Telemetry Sender Socket
        m_udpServerSendSocket = std::make_unique<asio::ip::udp::socket>(m_ioContext);
        m_udpServerSendSocket->open(asio::ip::udp::v4());
        m_udpServerSendSocket->set_option(asio::socket_base::broadcast(true));

        // Start ASIO event loop in background thread
        m_networkThread = std::thread([this]() {
            std::cout << "[Server Network] Thread started." << std::endl;
            m_ioContext.run();
            std::cout << "[Server Network] Thread stopped." << std::endl;
        });

        // Start accepting TCP connections
        startAccept();
        std::cout << "[Server Network] Listening on TCP port " << tcpPort << " and UDP port " << udpPort << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[Server Network] Exception during startup: " << e.what() << std::endl;
        stop();
    }
}

void AsioNetworkManager::startClient(const std::string& serverHost, uint16_t tcpPort, uint16_t udpPort) {
    if (m_isRunning) return;

    m_isServer = false;
    m_isRunning = true;

    try {
        m_serverSocket = std::make_shared<asio::ip::tcp::socket>(m_ioContext);
        
        // Setup UDP Telemetry Receiver Socket
        m_udpClientRecvSocket = std::make_unique<asio::ip::udp::socket>(m_ioContext);
        m_udpClientRecvSocket->open(asio::ip::udp::v4());
        m_udpClientRecvSocket->set_option(asio::socket_base::reuse_address(true));
        
        #if !defined(_WIN32)
        // Set reuse port for Unix systems to allow clean rebinding
        asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT> reusePort(true);
        m_udpClientRecvSocket->set_option(reusePort);
        #endif

        m_udpClientRecvSocket->bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), udpPort));
        m_udpRecvBuffer.resize(65536);

        // Resolve DNS / IP Address
        asio::ip::tcp::resolver resolver(m_ioContext);
        auto endpoints = resolver.resolve(serverHost, std::to_string(tcpPort));

        // Start ASIO event loop
        m_networkThread = std::thread([this]() {
            std::cout << "[Client Network] Thread started." << std::endl;
            m_ioContext.run();
            std::cout << "[Client Network] Thread stopped." << std::endl;
        });

        // Async connect to TCP server
        asio::async_connect(*m_serverSocket, endpoints, 
            [this, udpPort](const asio::error_code& ec, const asio::ip::tcp::endpoint& endpoint) {
                if (!ec) {
                    std::cout << "[Client Network] Connected to server at " << endpoint << std::endl;
                    
                    // Add server's IP with our UDP port to receive telemetry
                    std::lock_guard<std::mutex> lock(m_endpointsMutex);
                    m_udpSenderEndpoint = asio::ip::udp::endpoint(endpoint.address(), udpPort);

                    // Start reading incoming TCP server RPCs
                    startClientTcpRead();

                    // Start reading UDP telemetry slices
                    startUdpReceive();
                } else {
                    std::cerr << "[Client Network] Connection failed: " << ec.message() << std::endl;
                    stop();
                }
            }
        );
    }
    catch (const std::exception& e) {
        std::cerr << "[Client Network] Exception during startup: " << e.what() << std::endl;
        stop();
    }
}

void AsioNetworkManager::stop() {
    if (!m_isRunning) return;
    
    m_isRunning = false;

    // Terminate ASIO contexts
    m_ioContext.stop();

    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }

    // Reset io_context and rebuild guard for future reuse
    m_ioContext.restart();

    // Close TCP Acceptor
    if (m_tcpAcceptor) {
        asio::error_code ec;
        m_tcpAcceptor->close(ec);
        m_tcpAcceptor.reset();
    }

    // Close sockets
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& sock : m_clientSockets) {
            asio::error_code ec;
            sock->close(ec);
        }
        m_clientSockets.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_serverSocketMutex);
        if (m_serverSocket) {
            asio::error_code ec;
            m_serverSocket->close(ec);
            m_serverSocket.reset();
        }
    }

    if (m_udpServerSendSocket) {
        asio::error_code ec;
        m_udpServerSendSocket->close(ec);
        m_udpServerSendSocket.reset();
    }

    if (m_udpClientRecvSocket) {
        asio::error_code ec;
        m_udpClientRecvSocket->close(ec);
        m_udpClientRecvSocket.reset();
    }

    std::cout << "[Network] Sockets closed and service terminated." << std::endl;
}

void AsioNetworkManager::setTelemetryCallback(std::function<void(const Generative::EngineStateUBO&, const ColorRGBW*, uint32_t)> callback) {
    m_telemetryCallback = std::move(callback);
}

void AsioNetworkManager::setCommandCallback(std::function<void(CommandType, const std::string&)> callback) {
    m_commandCallback = std::move(callback);
}

void AsioNetworkManager::setClientConnectCallback(std::function<void(std::shared_ptr<asio::ip::tcp::socket>)> callback) {
    m_clientConnectCallback = std::move(callback);
}

// ─────────────────────────────────────────────────────────────────────
// SERVER TCP ACCEPT & CLIENT ROUTING
// ─────────────────────────────────────────────────────────────────────

void AsioNetworkManager::startAccept() {
    if (!m_tcpAcceptor) return;

    auto socket = std::make_shared<asio::ip::tcp::socket>(m_ioContext);
    m_tcpAcceptor->async_accept(*socket, 
        [this, socket](const asio::error_code& ec) {
            if (!ec) {
                std::cout << "[Server Network] Accepted connection from " << socket->remote_endpoint() << std::endl;
                
                // Add to connected clients list
                {
                    std::lock_guard<std::mutex> lock(m_clientsMutex);
                    m_clientSockets.push_back(socket);
                }

                // Register their IP as a telemetry client
                {
                    std::lock_guard<std::mutex> lock(m_endpointsMutex);
                    // Match the local telemetry port
                    uint16_t clientTelemetryPort = socket->remote_endpoint().port(); // fall back
                    // Better approach: assume default client telemetry port or ask client
                    // For now, client runs on standard port, so target the client IP + standard client UDP port
                    m_clientUdpEndpoints.push_back(asio::ip::udp::endpoint(socket->remote_endpoint().address(), 7778));
                }

                // Start listening to commands from this client
                readCommandHeader(socket);

                if (m_clientConnectCallback) {
                    m_clientConnectCallback(socket);
                }
            }
            // Continue accepting other clients
            startAccept();
        }
    );
}

void AsioNetworkManager::readCommandHeader(std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto header = std::make_shared<CommandHeader>();
    asio::async_read(*socket, asio::buffer(header.get(), sizeof(CommandHeader)),
        [this, socket, header](const asio::error_code& ec, size_t /*bytesRead*/) {
            if (!ec) {
                readCommandPayload(socket, *header);
            } else {
                // Connection closed or error
                std::cout << "[Server Network] Client disconnected or read error: " << ec.message() << std::endl;
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                m_clientSockets.erase(std::remove(m_clientSockets.begin(), m_clientSockets.end(), socket), m_clientSockets.end());
            }
        }
    );
}

void AsioNetworkManager::readCommandPayload(std::shared_ptr<asio::ip::tcp::socket> socket, CommandHeader header) {
    auto payloadBuf = std::make_shared<std::vector<char>>(header.payloadLength);
    asio::async_read(*socket, asio::buffer(payloadBuf->data(), header.payloadLength),
        [this, socket, header, payloadBuf](const asio::error_code& ec, size_t /*bytesRead*/) {
            if (!ec) {
                std::string payloadStr(payloadBuf->begin(), payloadBuf->end());
                
                // Route command to server main thread via callback
                if (m_commandCallback) {
                    m_commandCallback(header.type, payloadStr);
                }

                // Queue next read
                readCommandHeader(socket);
            } else {
                std::cout << "[Server Network] Error reading command payload: " << ec.message() << std::endl;
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                m_clientSockets.erase(std::remove(m_clientSockets.begin(), m_clientSockets.end(), socket), m_clientSockets.end());
            }
        }
    );
}

// ─────────────────────────────────────────────────────────────────────
// CLIENT TCP READ PIPELINE
// ─────────────────────────────────────────────────────────────────────

void AsioNetworkManager::startClientTcpRead() {
    readClientCommandHeader();
}

void AsioNetworkManager::readClientCommandHeader() {
    auto header = std::make_shared<CommandHeader>();
    asio::async_read(*m_serverSocket, asio::buffer(header.get(), sizeof(CommandHeader)),
        [this, header](const asio::error_code& ec, size_t /*bytesRead*/) {
            if (!ec) {
                readClientCommandPayload(*header);
            } else {
                std::cerr << "[Client Network] Lost connection to server: " << ec.message() << std::endl;
                stop();
            }
        }
    );
}

void AsioNetworkManager::readClientCommandPayload(CommandHeader header) {
    auto payloadBuf = std::make_shared<std::vector<char>>(header.payloadLength);
    asio::async_read(*m_serverSocket, asio::buffer(payloadBuf->data(), header.payloadLength),
        [this, header, payloadBuf](const asio::error_code& ec, size_t /*bytesRead*/) {
            if (!ec) {
                std::string payloadStr(payloadBuf->begin(), payloadBuf->end());
                
                // Dispatch command to Client handler
                if (m_commandCallback) {
                    m_commandCallback(header.type, payloadStr);
                }

                // Wait for next command
                readClientCommandHeader();
            } else {
                std::cerr << "[Client Network] Read error for command payload: " << ec.message() << std::endl;
                stop();
            }
        }
    );
}

// ─────────────────────────────────────────────────────────────────────
// COMMAND SEND & BROADCAST VIA TCP
// ─────────────────────────────────────────────────────────────────────

void AsioNetworkManager::sendCommandToServer(CommandType type, const std::string& jsonPayload) {
    std::lock_guard<std::mutex> lock(m_serverSocketMutex);
    if (!m_serverSocket || !m_serverSocket->is_open()) return;

    // Prepare packet buffer
    CommandHeader header{ (uint32_t)jsonPayload.size(), type };
    
    std::vector<asio::const_buffer> buffers;
    buffers.push_back(asio::buffer(&header, sizeof(CommandHeader)));
    buffers.push_back(asio::buffer(jsonPayload.data(), jsonPayload.size()));

    asio::error_code ec;
    asio::write(*m_serverSocket, buffers, ec);
    if (ec) {
        std::cerr << "[Client Network] Send command error: " << ec.message() << std::endl;
    }
}

void AsioNetworkManager::broadcastCommandToClients(CommandType type, const std::string& jsonPayload) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    if (m_clientSockets.empty()) return;

    CommandHeader header{ (uint32_t)jsonPayload.size(), type };

    // Capture payload in shared pointer to prevent deletion during async execution
    auto packetData = std::make_shared<std::vector<uint8_t>>(sizeof(CommandHeader) + jsonPayload.size());
    std::memcpy(packetData->data(), &header, sizeof(CommandHeader));
    std::memcpy(packetData->data() + sizeof(CommandHeader), jsonPayload.data(), jsonPayload.size());

    for (auto& socket : m_clientSockets) {
        if (socket->is_open()) {
            asio::async_write(*socket, asio::buffer(*packetData),
                [packetData](const asio::error_code& ec, size_t /*bytesWritten*/) {
                    if (ec) {
                        std::cerr << "[Server Network] Broadcast to client failed: " << ec.message() << std::endl;
                    }
                }
            );
        }
    }
}

void AsioNetworkManager::sendCommandToNewClient(std::shared_ptr<asio::ip::tcp::socket> socket, CommandType type, const std::string& jsonPayload) {
    if (!socket || !socket->is_open()) return;

    CommandHeader header{ (uint32_t)jsonPayload.size(), type };
    auto packetData = std::make_shared<std::vector<uint8_t>>(sizeof(CommandHeader) + jsonPayload.size());
    std::memcpy(packetData->data(), &header, sizeof(CommandHeader));
    std::memcpy(packetData->data() + sizeof(CommandHeader), jsonPayload.data(), jsonPayload.size());

    asio::async_write(*socket, asio::buffer(*packetData),
        [packetData, socket](const asio::error_code& ec, size_t /*bytesWritten*/) {
            if (ec) {
                std::cerr << "[Server Network] Initial sync to new client failed: " << ec.message() << std::endl;
            }
        }
    );
}

// ─────────────────────────────────────────────────────────────────────
// UDP TELEMETRY SLICING (SERVER)
// ─────────────────────────────────────────────────────────────────────

void AsioNetworkManager::streamTelemetry(uint32_t frameNumber, const Generative::EngineStateUBO& ubo, const ColorRGBW* pixels, uint32_t pixelCount) {
    if (!m_udpServerSendSocket || !m_udpServerSendSocket->is_open()) return;

    // Calculate buffer sizes
    uint32_t uboSize = sizeof(Generative::EngineStateUBO);
    uint32_t pixelSize = pixelCount * sizeof(ColorRGBW);
    uint32_t totalFrameSize = uboSize + pixelSize;

    // Create intermediate contiguous buffer to slice
    std::vector<uint8_t> frameBuffer(totalFrameSize);
    std::memcpy(frameBuffer.data(), &ubo, uboSize);
    if (pixelSize > 0) {
        std::memcpy(frameBuffer.data() + uboSize, pixels, pixelSize);
    }

    uint16_t sliceCount = (totalFrameSize + MAX_UDP_PAYLOAD_SIZE - 1) / MAX_UDP_PAYLOAD_SIZE;

    std::lock_guard<std::mutex> lock(m_endpointsMutex);
    if (m_clientUdpEndpoints.empty()) return;

    for (uint16_t i = 0; i < sliceCount; ++i) {
        uint32_t offset = i * MAX_UDP_PAYLOAD_SIZE;
        uint16_t payloadSize = std::min<uint32_t>(MAX_UDP_PAYLOAD_SIZE, totalFrameSize - offset);

        TelemetrySliceHeader header{
            TELEMETRY_SLICE_MAGIC,
            frameNumber,
            totalFrameSize,
            pixelCount,
            i,
            sliceCount,
            offset,
            payloadSize
        };

        // Create packet buffer
        auto packet = std::make_shared<std::vector<uint8_t>>(sizeof(TelemetrySliceHeader) + payloadSize);
        std::memcpy(packet->data(), &header, sizeof(TelemetrySliceHeader));
        std::memcpy(packet->data() + sizeof(TelemetrySliceHeader), frameBuffer.data() + offset, payloadSize);

        // Send to all clients
        for (const auto& endpoint : m_clientUdpEndpoints) {
            m_udpServerSendSocket->async_send_to(asio::buffer(*packet), endpoint,
                [packet](const asio::error_code& ec, size_t /*bytesSent*/) {
                    if (ec) {
                        // Suppress excessive logging in high-frequency loop
                    }
                }
            );
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// UDP TELEMETRY REASSEMBLY (CLIENT)
// ─────────────────────────────────────────────────────────────────────

void AsioNetworkManager::startUdpReceive() {
    if (!m_udpClientRecvSocket || !m_udpClientRecvSocket->is_open()) return;

    m_udpClientRecvSocket->async_receive_from(
        asio::buffer(m_udpRecvBuffer),
        m_udpSenderEndpoint,
        [this](const asio::error_code& ec, size_t bytesTransferred) {
            if (!ec) {
                processReceivedSlice(m_udpRecvBuffer.data(), bytesTransferred);
                startUdpReceive(); // Continue listening
            } else {
                if (m_isRunning) {
                    std::cerr << "[Client UDP] Receive error: " << ec.message() << std::endl;
                }
            }
        }
    );
}

void AsioNetworkManager::processReceivedSlice(const uint8_t* data, size_t bytesTransferred) {
    if (bytesTransferred < sizeof(TelemetrySliceHeader)) return;

    TelemetrySliceHeader header;
    std::memcpy(&header, data, sizeof(TelemetrySliceHeader));

    if (header.magic != TELEMETRY_SLICE_MAGIC) return;

    std::lock_guard<std::mutex> lock(m_reassemblyMutex);

    // If slice belongs to a newer frame, discard the current in-progress assembly
    if (header.frameNumber > m_reassembly.frameNumber || m_reassembly.frameNumber == 0xFFFFFFFF) {
        m_reassembly.frameNumber = header.frameNumber;
        m_reassembly.totalFrameSize = header.totalFrameSize;
        m_reassembly.pixelCount = header.pixelCount;
        m_reassembly.buffer.assign(header.totalFrameSize, 0);
        m_reassembly.receivedSlices.assign(header.sliceCount, false);
        m_reassembly.receivedCount = 0;
    }

    // Assemble if slice is part of the current active frame
    if (header.frameNumber == m_reassembly.frameNumber) {
        if (header.sliceIndex < m_reassembly.receivedSlices.size() && !m_reassembly.receivedSlices[header.sliceIndex]) {
            // Verify bounds before copy
            if (header.payloadOffset + header.payloadSize <= m_reassembly.totalFrameSize &&
                sizeof(TelemetrySliceHeader) + header.payloadSize <= bytesTransferred) {
                
                std::memcpy(
                    m_reassembly.buffer.data() + header.payloadOffset, 
                    data + sizeof(TelemetrySliceHeader), 
                    header.payloadSize
                );
                
                m_reassembly.receivedSlices[header.sliceIndex] = true;
                m_reassembly.receivedCount++;

                // Trigger callback when all slices are fully received
                if (m_reassembly.receivedCount == m_reassembly.receivedSlices.size()) {
                    if (m_telemetryCallback) {
                        // Extract UBO and Pixels pointer
                        const uint8_t* bufPtr = m_reassembly.buffer.data();
                        
                        Generative::EngineStateUBO ubo;
                        std::memcpy(&ubo, bufPtr, sizeof(Generative::EngineStateUBO));

                        const ColorRGBW* pixelsPtr = reinterpret_cast<const ColorRGBW*>(bufPtr + sizeof(Generative::EngineStateUBO));
                        
                        m_telemetryCallback(ubo, pixelsPtr, m_reassembly.pixelCount);
                    }
                }
            }
        }
    }
}

} // namespace PixelMapper::Network
