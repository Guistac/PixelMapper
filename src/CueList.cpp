#include "CueList.h"
#include "Patch.h"
#include "App.h"

namespace PixelMapper {
namespace CueList {

void import(flecs::world& w) {
    w.component<List>();

    // ── CueAdvancer system ──────────────────────────────────────────
    // Runs on the main thread in PreStore (~once per frame).
    // When auto-advance is active it accumulates time and triggers a
    // cue switch by writing the new GLSL source into ScriptData and
    // marking ProgramDirty.  No crossfade — just a hard switch.
    w.system<CueList::List, Patch::ScriptData>("CueAdvancer")
     .with<Patch::Is>()
     .kind(flecs::PreStore)
     .each([](flecs::iter& it, size_t i, CueList::List& list, Patch::ScriptData& sd)
     {
         if (!list.autoAdvance)           return;
         if (list.cues.empty())           return;
         if (list.activeIndex < 0)        return;

         list.holdTimer += it.delta_time();

         const Cue& activeCue = list.cues[list.activeIndex];
         if (list.holdTimer < activeCue.holdSeconds) return;

         // Advance to next cue
         list.holdTimer = 0.0f;
         int next = list.activeIndex + 1;
         if (next >= (int)list.cues.size()) {
             if (list.loop) next = 0;
             else { list.autoAdvance = false; return; }
         }
         list.activeIndex = next;

         // Push new GLSL source and recompile with crossfade
         const Cue& nextCue = list.cues[next];
         sd.glslSource = nextCue.glslSource;
         App::pendingCrossfadeDuration = nextCue.fadeSeconds;
         it.entity(i).add<Patch::ProgramDirty>();
     });
}

} // namespace CueList
} // namespace PixelMapper
