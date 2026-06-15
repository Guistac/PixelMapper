#include "EffectBank.h"

namespace PixelMapper {
namespace EffectBank {

void import(flecs::world& w) {
    w.component<Bank>();
}

} // namespace EffectBank
} // namespace PixelMapper
