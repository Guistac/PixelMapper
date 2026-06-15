#include "ArtnetSender.h"
#include "Patch.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

namespace PixelMapper {

ArtnetSender::ArtnetSender() : socketFd(-1), activeSourcePort(0) {}

ArtnetSender::~ArtnetSender() {
    closeSocket();
}

void ArtnetSender::closeSocket() {
    if (socketFd != -1) {
        close(socketFd);
        socketFd = -1;
    }
    activeSourcePort = 0;
}

void ArtnetSender::send(const PatchProgram* program) {
    if (!program) return;

    // 1. Manage socket setup/re-binding if source port changes
    if (socketFd == -1 || activeSourcePort != program->sourcePort) {
        if (socketFd != -1) {
            closeSocket();
        }

        socketFd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socketFd >= 0) {
            int broadcastEnable = 1;
            setsockopt(socketFd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

            sockaddr_in localAddr{};
            localAddr.sin_family = AF_INET;
            localAddr.sin_port = htons(program->sourcePort);
            localAddr.sin_addr.s_addr = INADDR_ANY;

            if (bind(socketFd, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
                // Bind failed, matching original behavior (fails silently or doesn't bind)
            }
            activeSourcePort = program->sourcePort;
        }
    }

    if (socketFd < 0) return;

    // 2. Prepare Art-Net Packet and Send
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

    for (uint32_t d = 0; d < program->deviceCount; d++) {
        const auto& device = program->devices[d];
        
        sockaddr_in destAddr{};
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(6454);
        destAddr.sin_addr.s_addr = device.ipAddress;

        for (uint32_t u = 0; u < program->universeCount; u++) {
            const auto& universe = program->universes[u];
            if (universe.id >= device.startUniverse && 
                universe.id < device.startUniverse + device.universeCount) {
                
                ArtDmxHeader header;
                header.universe = universe.id;

                uint8_t packet[sizeof(ArtDmxHeader) + 512];
                std::memcpy(packet, &header, sizeof(header));
                std::memcpy(packet + sizeof(header), universe.buffer, 512);

                sendto(socketFd, packet, sizeof(packet), 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
            }
        }
    }
}

} // namespace PixelMapper
