#pragma once
#include <stdint.h>

namespace PixelMapper {

struct PatchProgram;

class ArtnetSender {
public:
    ArtnetSender();
    ~ArtnetSender();

    void send(const PatchProgram* program);
    void closeSocket();

private:
    int socketFd = -1;
    uint16_t activeSourcePort = 0;
};

} // namespace PixelMapper
