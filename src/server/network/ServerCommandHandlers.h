#pragma once
#include <flecs.h>
#include <string>
#include <vector>
#include "shared/network/CommandProtocol.h"

namespace PixelMapper::Network {

struct ErrorMarker {
    int line;
    std::string message;
};

void handleServerCommand(CommandType type, const std::string& jsonPayload, flecs::world& world);

bool compileGLSLHeadless(const std::string& source, std::string& outLog, std::vector<ErrorMarker>& outErrors);
bool compileLuaHeadless(const std::string& source, std::string& outLog, std::vector<ErrorMarker>& outErrors);

} // namespace PixelMapper::Network
