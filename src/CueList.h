#pragma once
#include <flecs.h>
#include <string>
#include <vector>

namespace PixelMapper {
namespace CueList {

struct Cue {
    std::string name;
    std::string glslSource;   ///< GLSL fragment shader for this cue
    float holdSeconds = 5.0f; ///< How long to hold before auto-advancing
    float fadeSeconds = 0.0f; ///< (reserved for future crossfade)
};

struct List {
    std::vector<Cue> cues;
    int  activeIndex  = -1;   ///< Currently active cue (-1 = none)
    bool autoAdvance  = false; ///< Advance to next cue after holdSeconds
    bool loop         = true;  ///< Wrap around at end
    float holdTimer   = 0.0f; ///< Accumulated hold time (seconds)
};

/// Register components and the CueAdvancer system.
void import(flecs::world& w);

} // namespace CueList
} // namespace PixelMapper
