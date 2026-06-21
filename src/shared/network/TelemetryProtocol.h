#pragma once
#include <stdint.h>
#include <glm/glm.hpp>
#include "shared/Common.h"

namespace PixelMapper {

namespace Generative {

    // C++ memory layout matching GLSL std140 for color stops
    struct alignas(16) ColorStop {
        glm::vec4 color;       // 16 bytes (RGBA)
        float position;        // 4 bytes [0.0 - 1.0]
        float smoothness;      // 4 bytes [0.0 - 1.0]
        glm::vec2 padding;     // 8 bytes padding
    };

    // C++ memory layout matching GLSL std140 for engine state
    struct alignas(16) EngineStateUBO {
        int activeStops;       // 4 bytes
        glm::ivec3 _pad0;      // 12 bytes padding
        ColorStop palette[16]; // 512 bytes (16 * 32 bytes)
        
        // Motive parameters
        float velocity;        // 4 bytes
        float complexity;      // 4 bytes
        float scale;           // 4 bytes
        float distortion;      // 4 bytes
        float asymmetry;       // 4 bytes
        float intensity;       // 4 bytes
        glm::vec2 _pad1;       // 8 bytes padding
    };

} // namespace Generative

namespace Network {

#define TELEMETRY_SLICE_MAGIC 0x50584D53 // "PXMS" in ASCII
constexpr uint16_t MAX_UDP_PAYLOAD_SIZE = 1200; // Keep safely below 1500 bytes MTU

#pragma pack(push, 1)
struct TelemetrySliceHeader {
    uint32_t magic;             // Safety identifier (TELEMETRY_SLICE_MAGIC)
    uint32_t frameNumber;       // Monotonically increasing frame index
    uint32_t totalFrameSize;    // Total size of reconstructed telemetry data
    uint32_t pixelCount;        // Number of pixels in the frame
    uint16_t sliceIndex;        // Sequence number of current slice (0-based)
    uint16_t sliceCount;        // Total number of slices for this frame
    uint32_t payloadOffset;     // Offset in reconstructed buffer
    uint16_t payloadSize;       // Payload size in bytes
};
#pragma pack(pop)

} // namespace Network
} // namespace PixelMapper
