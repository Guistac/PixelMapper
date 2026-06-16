#include "CueList.h"
#include "Patch.h"
#include "App.h"
#include <vector>
#include <algorithm>

namespace PixelMapper {
namespace CueList {

void import(flecs::world& w) {
    w.component<Is>();
    w.component<CueFolder>();
    w.component<Cue::Is>();
    w.component<Cue::HoldDuration>();
    w.component<Cue::FadeDuration>();
    w.component<Cue::IndexOrder>();
    w.component<Cue::TargetEffect>();
    w.component<SessionState>();

    // ── CueAdvancer system ──────────────────────────────────────────
    w.system<SessionState>("CueAdvancer")
     .kind(flecs::PreStore)
     .each([](flecs::iter& it, size_t i, SessionState& session)
     {
         if (!session.autoAdvance) return;
         flecs::entity folder = it.entity(i);

         struct SortEntry {
             flecs::entity entity;
             int order = 0;
             float hold = 5.0f;
             float fade = 0.0f;
         };
         std::vector<SortEntry> sortedCues;

         folder.children([&](flecs::entity child) {
             if (child.has<Cue::Is>()) {
                 SortEntry entry;
                 entry.entity = child;
                 if (const auto* ord = child.try_get<Cue::IndexOrder>()) entry.order = ord->value;
                 if (const auto* h = child.try_get<Cue::HoldDuration>()) entry.hold = h->value;
                 if (const auto* f = child.try_get<Cue::FadeDuration>()) entry.fade = f->value;
                 sortedCues.push_back(entry);
             }
         });

         if (sortedCues.empty()) return;

         std::sort(sortedCues.begin(), sortedCues.end(), [](const SortEntry& a, const SortEntry& b) {
             return a.order < b.order;
         });

         if (session.activeIndex < 0 || session.activeIndex >= (int)sortedCues.size()) return;

         session.holdTimer += it.delta_time();
         float currentHold = sortedCues[session.activeIndex].hold;
         if (session.holdTimer < currentHold) return;

         // Advance to next cue
         session.holdTimer = 0.0f;
         int next = session.activeIndex + 1;
         if (next >= (int)sortedCues.size()) {
             if (session.loop) next = 0;
             else { session.autoAdvance = false; return; }
         }
         session.activeIndex = next;

         float nextFade = sortedCues[next].fade;

         // Write the next active cue index atomically
         auto program = std::atomic_load(&App::currentPatchProgram);
         if (program) {
             program->pendingCrossfadeDuration.store(nextFade);
             program->activeCueIndex.store(next);
         }
     });
}

} // namespace CueList
} // namespace PixelMapper
