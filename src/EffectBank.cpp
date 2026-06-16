#include "EffectBank.h"

namespace PixelMapper {
namespace EffectBank {

void import(flecs::world& w) {
    w.component<Is>();
    w.component<EffectFolder>();
    w.component<Effect::Is>();
    w.component<Effect::GlslSource>();
    w.component<SessionState>();
}

} // namespace EffectBank
} // namespace PixelMapper
