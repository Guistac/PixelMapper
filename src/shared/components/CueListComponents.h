#pragma once
#include <flecs.h>

namespace PixelMapper {
namespace CueList {

    struct Is {};
    struct CueFolder {};

    namespace Cue {
        struct Is {};
        struct HoldDuration { float value; };
        struct FadeDuration { float value; };
        struct IndexOrder { int value; };
        struct TargetEffect {}; // Used in relationship pair (TargetEffect, effect_entity)
    }

    struct SessionState {
        int activeIndex = -1;
        bool autoAdvance = false;
        bool loop = true;
        float holdTimer = 0.0f;
    };

} // namespace CueList
} // namespace PixelMapper
