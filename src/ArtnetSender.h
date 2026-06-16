#pragma once
#include <stdint.h>
#include <memory>
#include <mutex>
#include <string>

namespace Network {
    class UdpSocket;
}

namespace PixelMapper {

struct PatchProgram;

class ArtnetSender {
public:
    ArtnetSender();
    ~ArtnetSender();

    void send(const PatchProgram* program);
    void closeSocket();

private:
    std::unique_ptr<Network::UdpSocket> udpSocket;
    uint16_t activeSourcePort = 0;

    std::mutex errorMutex;
    bool hadSendError = false;
    std::string lastSendError;
};

} // namespace PixelMapper
