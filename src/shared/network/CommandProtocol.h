#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace PixelMapper::Network {

enum class CommandType : uint16_t {
    SyncWorldState = 1,       // Server -> Client: Full JSON representation of the world
    EntityMutation = 2,       // Client -> Server: Flecs JSON component edit
    ApplyScript = 3,          // Client -> Server: Compile Lua/GLSL code (Payload: {"path": "...", "type": "GLSL"|"LUA", "source": "..."})
    CompilationResult = 4,    // Server -> Client: Success/Failure with error line info (Payload: {"success": bool, "log": "...", "errors": [{"line": X, "message": "..."}]})
    FileIORequest = 5,        // Client -> Server: Save/Load trigger (Payload: {"action": "SAVE"|"LOAD", "path": "..."})
    TransportControl = 6      // Client -> Server: Cue List sequencer state change (Payload: {"cueIndex": X})
};

#pragma pack(push, 1)
struct CommandHeader {
    uint32_t payloadLength;   // Length of JSON payload in bytes
    CommandType type;         // The RPC code
};
#pragma pack(pop)

} // namespace PixelMapper::Network
