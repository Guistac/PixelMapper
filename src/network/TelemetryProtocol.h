#pragma once
#include <stdint.h>
#include "../Common.h"
#include "../GenerativeEngine.h"

namespace PixelMapper::Network {

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

} // namespace PixelMapper::Network
