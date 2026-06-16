#include "ArtnetSender.h"
#include "Patch.h"
#include "App.h"
#include "Network.h"

#include <cstring>
#include <algorithm>
#include <iostream>
#include <mutex>

#if defined(__APPLE__) || defined(__linux__)
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif

namespace PixelMapper {

ArtnetSender::ArtnetSender() : activeSourcePort(0), hadSendError(false) {
    udpSocket = std::make_unique<Network::UdpSocket>();
    
    // Register the callback to update the error state asynchronously
    udpSocket->setErrorCallback([this](const std::string& err) {
        std::lock_guard<std::mutex> lock(errorMutex);
        hadSendError = true;
        lastSendError = err;
    });
}

ArtnetSender::~ArtnetSender() {
    closeSocket();
}

void ArtnetSender::closeSocket() {
    if (udpSocket && udpSocket->isOpen()) {
        udpSocket->close();
    }
    activeSourcePort = 0;
}

static void updateNetworkStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(App::rtNetworkStatusMutex);
    std::strncpy(App::rtNetworkStatus, status.c_str(), sizeof(App::rtNetworkStatus) - 1);
    App::rtNetworkStatus[sizeof(App::rtNetworkStatus) - 1] = '\0';
}

void ArtnetSender::send(const PatchProgram* program) {
    if (!program) return;

    // 1. Read and clear error status from previous frame sends
    bool frameHadError = false;
    std::string frameLastError = "";
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        frameHadError = hadSendError;
        frameLastError = lastSendError;
        hadSendError = false;
    }

    // 2. Manage socket setup/re-binding if source port changes
    if (!udpSocket->isOpen() || activeSourcePort != program->sourcePort) {
        if (udpSocket->isOpen()) {
            closeSocket();
        }

        udpSocket->open();
        if (udpSocket->isOpen()) {
            if (!udpSocket->bind(0, program->sourcePort)) {
                std::cerr << "[ArtnetSender] Warning: bind to port " << program->sourcePort << " failed. Retrying with ephemeral port...\n";
                
                closeSocket();
                udpSocket->open();
                if (!udpSocket->bind(0, 0)) {
                    std::cerr << "[ArtnetSender] Error: bind to ephemeral port failed\n";
                    updateNetworkStatus("Bind failed");
                    return;
                }
            }
            activeSourcePort = program->sourcePort;
        } else {
            updateNetworkStatus("Socket creation failed");
            return;
        }
    }

    if (!udpSocket->isOpen()) return;

    // Get socket bound address and port to show in GUI
    std::string boundAddressStr = udpSocket->getLocalAddress() + ":" + std::to_string(udpSocket->getLocalPort());

    // 4. Prepare Art-Net Packet and Send
#pragma pack(push, 1)
    struct ArtDmxHeader {
        char id[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};
        uint16_t opCode = 0x5000;
        uint16_t protVer = htons(14);
        uint8_t sequence = 0x00;
        uint8_t physical = 0x00;
        uint16_t universe = 0;
        uint16_t length = htons(512);
    };
#pragma pack(pop)

    std::string statusMsg = "Active (" + boundAddressStr + "). ";
    bool anyDevices = false;

    for (uint32_t d = 0; d < program->deviceCount; d++) {
        const auto& device = program->devices[d];
        anyDevices = true;
        
        // Revert htonl swap since device.ipAddress is already stored in memory network byte order
        // UdpSocket::send expects IP in host byte order.
        uint32_t hostIp = ntohl(device.ipAddress);

        for (uint32_t u = 0; u < program->universeCount; u++) {
            const auto& universe = program->universes[u];
            if (universe.id >= device.startUniverse && 
                universe.id < device.startUniverse + device.universeCount) {
                
                ArtDmxHeader header;
                header.universe = universe.id;

                uint8_t packet[sizeof(ArtDmxHeader) + 512];
                std::memcpy(packet, &header, sizeof(header));
                std::memcpy(packet + sizeof(header), universe.buffer, 512);

                udpSocket->send(packet, sizeof(packet), hostIp, 6454);
            }
        }
    }

    if (!anyDevices) {
        statusMsg += "No devices configured.";
    } else if (frameHadError) {
        statusMsg += "Send error: " + frameLastError;
    } else {
        statusMsg += "Sending ArtNet OK.";
    }

    updateNetworkStatus(statusMsg);
}

} // namespace PixelMapper
