#pragma once
#include <flecs.h>
#include <string>

namespace PixelMapper {

namespace PatchSerializer {
    /// Save all patches under pixelMapper to an XML file.
    /// Creates parent directories automatically.
    bool save(flecs::entity pixelMapper, const std::string& path);

    /// Load patches from an XML file into pixelMapper.
    /// Replaces any existing patches.
    bool load(flecs::entity pixelMapper, const std::string& path);
}

} // namespace PixelMapper
