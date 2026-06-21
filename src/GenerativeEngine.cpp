#include "GenerativeEngine.h"
#include "Patch.h"
#include <chrono>
#include <cmath>
#include <mutex>

namespace PixelMapper {

    static glm::vec4 samplePaletteStops(const std::vector<Generative::ColorStop>& stops, float p, float& outSmoothness) {
        if (stops.empty()) {
            outSmoothness = 0.5f;
            return glm::vec4(0.0f);
        }
        if (stops.size() == 1) {
            outSmoothness = stops[0].smoothness;
            return stops[0].color;
        }
        
        p = glm::clamp(p, 0.0f, 1.0f);
        for (size_t i = 0; i < stops.size() - 1; i++) {
            float p0 = stops[i].position;
            float p1 = stops[i+1].position;
            if (p >= p0 && p <= p1) {
                float blend = (p - p0) / std::max(0.0001f, p1 - p0);
                outSmoothness = glm::mix(stops[i].smoothness, stops[i+1].smoothness, blend);
                float width = outSmoothness;
                float f;
                if (width > 0.001f) {
                    float edge0 = 0.5f - width * 0.5f;
                    float val = glm::clamp((blend - edge0) / width, 0.0f, 1.0f);
                    f = val * val * (3.0f - 2.0f * val);
                } else {
                    f = (blend < 0.5f) ? 0.0f : 1.0f;
                }
                return glm::mix(stops[i].color, stops[i+1].color, f);
            }
        }
        outSmoothness = stops.back().smoothness;
        return stops.back().color;
    }

namespace Generative {
    void import(flecs::world& w) {
        w.component<Is>();
        w.component<PaletteFolder>();
        w.component<MotiveFolder>();
        
        w.component<Palette::Is>();
        w.component<Palette::Stops>();
        w.component<Palette::IsModeB>();
        
        w.component<Motive::Is>();
        w.component<Motive::Params>();
        
        w.component<Settings>();
    }
}

GenerativeEngineRuntime::GenerativeEngineRuntime() {
    auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    m_rng.seed((unsigned int)seed);
    
    // Clear UBO struct memory
    std::memset(&m_uboState, 0, sizeof(m_uboState));
}

void GenerativeEngineRuntime::init(PatchProgram* program) {
    m_paletteTimer = 0.0f;
    m_motiveTimer = 0.0f;
    m_shaderTimer = 0.0f;

    m_paletteFadeProgress = 1.0f;
    m_motiveFadeProgress = 1.0f;
    m_shaderFadeProgress = 1.0f;

    m_activePaletteIdx = -1;
    m_targetPaletteIdx = -1;

    m_activeMotiveIdx = -1;
    m_targetMotiveIdx = -1;

    m_activeShaderIdx = -1;
    m_targetShaderIdx = -1;
    m_shaderTransitionType = 0;

    m_paletteShuffleBag.clear();
    m_motiveShuffleBag.clear();
    m_shaderShuffleBag.clear();

    std::memset(m_currentAnchors, 0, sizeof(m_currentAnchors));
    std::memset(m_targetAnchors, 0, sizeof(m_targetAnchors));
    
    std::uniform_real_distribution<float> wanderDist(0.0f, 10000.0f);
    for (int k = 0; k < 6; ++k) {
        m_wanderTime[k] = wanderDist(m_rng);
    }

    std::memset(&m_uboState, 0, sizeof(m_uboState));
    m_uboState.velocity = 0.5f;
    m_uboState.complexity = 0.5f;
    m_uboState.scale = 0.5f;
    m_uboState.distortion = 0.5f;
    m_uboState.asymmetry = 0.5f;
    m_uboState.intensity = 0.5f;

    m_initialized = true;
}

void GenerativeEngineRuntime::updateShuffleBag(std::vector<int>& bag, size_t poolSize) {
    bag.clear();
    if (poolSize == 0) return;
    for (size_t i = 0; i < poolSize; i++) {
        bag.push_back((int)i);
    }
    std::shuffle(bag.begin(), bag.end(), m_rng);
}

int GenerativeEngineRuntime::drawFromShuffleBag(std::vector<int>& bag, size_t poolSize) {
    if (poolSize == 0) return -1;
    if (bag.empty()) {
        updateShuffleBag(bag, poolSize);
    }
    if (bag.empty()) return -1;
    int val = bag.back();
    bag.pop_back();
    return val;
}

void GenerativeEngineRuntime::update(float dt, PatchProgram* program) {
    if (!m_initialized) {
        init(program);
    }

    std::lock_guard<std::mutex> lock(program->generativeMutex);

    const auto& settings = program->generativeSettings;
    if (!settings.masterEnabled) {
        return;
    }

    // ────────────────────────────────────────────────────────────────
    // 1. PALETTE QUEUE UPDATE
    // ────────────────────────────────────────────────────────────────
    if (!program->palettePool.empty()) {
        // Initialize if not set
        if (m_activePaletteIdx.load() == -1 || m_activePaletteIdx.load() >= (int)program->palettePool.size()) {
            m_activePaletteIdx.store(drawFromShuffleBag(m_paletteShuffleBag, program->palettePool.size()));
            m_targetPaletteIdx.store(-1);
            m_paletteFadeProgress.store(1.0f);
            m_blendSize = 0;
            m_paletteTimer.store(settings.paletteInterval + std::uniform_real_distribution<float>(-settings.paletteJitter, settings.paletteJitter)(m_rng));
        }

        // Handle overrides vs timers
        if (settings.manualPaletteOverride && settings.manualPaletteIndex >= 0 && settings.manualPaletteIndex < (int)program->palettePool.size()) {
            if (m_activePaletteIdx.load() != settings.manualPaletteIndex && m_targetPaletteIdx.load() != settings.manualPaletteIndex) {
                startPaletteTransition(settings.manualPaletteIndex, program);
            }
        } else {
            // Automatic advancement
            if (m_paletteFadeProgress.load() >= 1.0f) {
                if (settings.paletteEnabled) {
                    m_paletteTimer.store(m_paletteTimer.load() - dt);
                    if (m_paletteTimer.load() <= 0.0f) {
                        int nextIdx = drawFromShuffleBag(m_paletteShuffleBag, program->palettePool.size());
                        if (nextIdx != m_activePaletteIdx.load() && nextIdx >= 0) {
                            startPaletteTransition(nextIdx, program);
                        }
                        m_paletteTimer.store(settings.paletteInterval + std::uniform_real_distribution<float>(-settings.paletteJitter, settings.paletteJitter)(m_rng));
                    }
                }
            }
        }

        // Advance fade
        if (m_paletteFadeProgress.load() < 1.0f) {
            if (settings.paletteEnabled || settings.manualPaletteOverride) {
                m_paletteFadeProgress.store(m_paletteFadeProgress.load() + dt / std::max(0.001f, m_paletteFadeDuration.load()));
                if (m_paletteFadeProgress.load() >= 1.0f) {
                    m_paletteFadeProgress.store(1.0f);
                    m_activePaletteIdx.store(m_targetPaletteIdx.load());
                    m_targetPaletteIdx.store(-1);
                    m_blendSize = 0;
                }
            }
        }

        // Evaluate Morphing Stops
        if (m_paletteFadeProgress.load() < 1.0f && m_targetPaletteIdx.load() >= 0 && m_targetPaletteIdx.load() < (int)program->palettePool.size()) {
            float eased_t = glm::smoothstep(0.0f, 1.0f, m_paletteFadeProgress.load());
            m_uboState.activeStops = m_blendSize;
            for (int i = 0; i < m_blendSize; i++) {
                m_uboState.palette[i].position = glm::mix(m_paddedA[i].position, m_paddedB[i].position, eased_t);
                m_uboState.palette[i].smoothness = glm::mix(m_paddedA[i].smoothness, m_paddedB[i].smoothness, eased_t);
                m_uboState.palette[i].color = glm::mix(m_paddedA[i].color, m_paddedB[i].color, eased_t);
            }
        } else {
            const auto& activePal = program->palettePool[m_activePaletteIdx.load()];
            m_uboState.activeStops = std::min((int)activePal.stops.size(), 16);
            for (int i = 0; i < m_uboState.activeStops; i++) {
                m_uboState.palette[i] = activePal.stops[i];
            }
        }
    }

    // ────────────────────────────────────────────────────────────────
    // 2. MOTIVE QUEUE UPDATE
    // ────────────────────────────────────────────────────────────────
    if (!program->motivePool.empty()) {
        if (m_activeMotiveIdx.load() == -1 || m_activeMotiveIdx.load() >= (int)program->motivePool.size()) {
            m_activeMotiveIdx.store(drawFromShuffleBag(m_motiveShuffleBag, program->motivePool.size()));
            m_targetMotiveIdx.store(-1);
            m_motiveFadeProgress.store(1.0f);
            
            const auto& act = program->motivePool[m_activeMotiveIdx.load()];
            m_currentAnchors[0] = act.velocity;
            m_currentAnchors[1] = act.complexity;
            m_currentAnchors[2] = act.scale;
            m_currentAnchors[3] = act.distortion;
            m_currentAnchors[4] = act.asymmetry;
            m_currentAnchors[5] = act.intensity;

            m_motiveTimer.store(settings.motiveInterval + std::uniform_real_distribution<float>(-settings.motiveJitter, settings.motiveJitter)(m_rng));
        }

        // Overrides
        if (settings.manualMotiveOverride && settings.manualMotiveIndex >= 0 && settings.manualMotiveIndex < (int)program->motivePool.size()) {
            if (m_activeMotiveIdx.load() != settings.manualMotiveIndex && m_targetMotiveIdx.load() != settings.manualMotiveIndex) {
                m_motiveFadeProgress.store(0.0f);
                m_motiveFadeDuration.store(settings.motiveCrossfade);
                m_targetMotiveIdx.store(settings.manualMotiveIndex);
                
                const auto& act = program->motivePool[m_activeMotiveIdx.load()];
                m_currentAnchors[0] = act.velocity;
                m_currentAnchors[1] = act.complexity;
                m_currentAnchors[2] = act.scale;
                m_currentAnchors[3] = act.distortion;
                m_currentAnchors[4] = act.asymmetry;
                m_currentAnchors[5] = act.intensity;

                const auto& tgt = program->motivePool[m_targetMotiveIdx.load()];
                m_targetAnchors[0] = tgt.velocity;
                m_targetAnchors[1] = tgt.complexity;
                m_targetAnchors[2] = tgt.scale;
                m_targetAnchors[3] = tgt.distortion;
                m_targetAnchors[4] = tgt.asymmetry;
                m_targetAnchors[5] = tgt.intensity;
            }
        } else {
            // Automatic
            if (m_motiveFadeProgress.load() >= 1.0f) {
                if (settings.motiveEnabled) {
                    m_motiveTimer.store(m_motiveTimer.load() - dt);
                    if (m_motiveTimer.load() <= 0.0f) {
                        int nextIdx = drawFromShuffleBag(m_motiveShuffleBag, program->motivePool.size());
                        if (nextIdx != m_activeMotiveIdx.load() && nextIdx >= 0) {
                            m_motiveFadeProgress.store(0.0f);
                            m_motiveFadeDuration.store(settings.motiveCrossfade);
                            m_targetMotiveIdx.store(nextIdx);
                            
                            const auto& act = program->motivePool[m_activeMotiveIdx.load()];
                            m_currentAnchors[0] = act.velocity;
                            m_currentAnchors[1] = act.complexity;
                            m_currentAnchors[2] = act.scale;
                            m_currentAnchors[3] = act.distortion;
                            m_currentAnchors[4] = act.asymmetry;
                            m_currentAnchors[5] = act.intensity;

                            const auto& tgt = program->motivePool[m_targetMotiveIdx.load()];
                            m_targetAnchors[0] = tgt.velocity;
                            m_targetAnchors[1] = tgt.complexity;
                            m_targetAnchors[2] = tgt.scale;
                            m_targetAnchors[3] = tgt.distortion;
                            m_targetAnchors[4] = tgt.asymmetry;
                            m_targetAnchors[5] = tgt.intensity;
                        }
                        m_motiveTimer.store(settings.motiveInterval + std::uniform_real_distribution<float>(-settings.motiveJitter, settings.motiveJitter)(m_rng));
                    }
                }
            }
        }

        // Fade Motives
        if (m_motiveFadeProgress.load() < 1.0f) {
            if (settings.motiveEnabled || settings.manualMotiveOverride) {
                m_motiveFadeProgress.store(m_motiveFadeProgress.load() + dt / std::max(0.001f, m_motiveFadeDuration.load()));
                if (m_motiveFadeProgress.load() >= 1.0f) {
                    m_motiveFadeProgress.store(1.0f);
                    m_activeMotiveIdx.store(m_targetMotiveIdx.load());
                    m_targetMotiveIdx.store(-1);
                    
                    const auto& act = program->motivePool[m_activeMotiveIdx.load()];
                    m_currentAnchors[0] = act.velocity;
                    m_currentAnchors[1] = act.complexity;
                    m_currentAnchors[2] = act.scale;
                    m_currentAnchors[3] = act.distortion;
                    m_currentAnchors[4] = act.asymmetry;
                    m_currentAnchors[5] = act.intensity;
                }
            }
        }

        // Compute Motive Interpolation & Wander
        const auto& activeMot = program->motivePool[m_activeMotiveIdx.load()];
        
        float finalVals[6];
        for (int k = 0; k < 6; k++) {
            float anchorVal = m_currentAnchors[k];
            float amp = activeMot.wanderAmp[k];
            float freq = activeMot.wanderFreq[k];

            if (m_motiveFadeProgress.load() < 1.0f && m_targetMotiveIdx.load() >= 0) {
                const auto& targetMot = program->motivePool[m_targetMotiveIdx.load()];
                float easedT = glm::smoothstep(0.0f, 1.0f, m_motiveFadeProgress.load());
                anchorVal = glm::mix(m_currentAnchors[k], m_targetAnchors[k], easedT);
                
                amp = glm::mix(activeMot.wanderAmp[k], targetMot.wanderAmp[k], m_motiveFadeProgress.load());
                freq = glm::mix(activeMot.wanderFreq[k], targetMot.wanderFreq[k], m_motiveFadeProgress.load());
            }

            // Wander LFO/Noise
            if (settings.motiveEnabled || settings.manualMotiveOverride) {
                m_wanderTime[k] += dt * freq;
            }
            float noiseVal = Noise1D::noise(m_wanderTime[k]);
            float wanderVal = noiseVal * amp;

            finalVals[k] = glm::clamp(anchorVal + wanderVal, 0.0f, 1.0f);
        }

        m_uboState.velocity = finalVals[0];
        m_uboState.complexity = finalVals[1];
        m_uboState.scale = finalVals[2];
        m_uboState.distortion = finalVals[3];
        m_uboState.asymmetry = finalVals[4];
        m_uboState.intensity = finalVals[5];
    }

    // ────────────────────────────────────────────────────────────────
    // 3. SHADER QUEUE UPDATE
    // ────────────────────────────────────────────────────────────────
    size_t shaderPoolSize = program->compiledCues.size();
    if (shaderPoolSize > 0) {
        if (m_activeShaderIdx.load() == -1 || m_activeShaderIdx.load() >= (int)shaderPoolSize) {
            m_activeShaderIdx.store(drawFromShuffleBag(m_shaderShuffleBag, shaderPoolSize));
            m_targetShaderIdx.store(-1);
            m_shaderFadeProgress.store(1.0f);
            m_shaderTimer.store(settings.shaderInterval + std::uniform_real_distribution<float>(-settings.shaderJitter, settings.shaderJitter)(m_rng));
        }

        // Overrides
        if (settings.manualShaderOverride && settings.manualShaderIndex >= 0 && settings.manualShaderIndex < (int)shaderPoolSize) {
            if (m_activeShaderIdx.load() != settings.manualShaderIndex && m_targetShaderIdx.load() != settings.manualShaderIndex) {
                m_shaderFadeProgress.store(0.0f);
                m_shaderFadeDuration.store(settings.shaderCrossfade);
                m_targetShaderIdx.store(settings.manualShaderIndex);
                
                // Select Transition
                if (settings.manualTransitionType >= 0) {
                    m_shaderTransitionType.store(settings.manualTransitionType);
                } else {
                    std::vector<int> enabledTypes;
                    if (settings.enableLinearDissolve) enabledTypes.push_back(0);
                    if (settings.enableLumaWipe) enabledTypes.push_back(1);
                    if (settings.enableSweep) enabledTypes.push_back(2);
                    if (settings.enableCircleWipe) enabledTypes.push_back(3);
                    if (settings.enableLuminosityWipe) enabledTypes.push_back(4);
                    
                    if (!enabledTypes.empty()) {
                        std::uniform_int_distribution<int> transDist(0, (int)enabledTypes.size() - 1);
                        m_shaderTransitionType.store(enabledTypes[transDist(m_rng)]);
                    } else {
                        m_shaderTransitionType.store(0); // fallback linear
                    }
                }
                randomizeTransitionDirections(program);
            }
        } else {
            // Automatic
            if (m_shaderFadeProgress.load() >= 1.0f) {
                if (settings.shaderEnabled && program->activeCueIndex.load() < 0) {
                    m_shaderTimer.store(m_shaderTimer.load() - dt);
                    if (m_shaderTimer.load() <= 0.0f) {
                        int nextIdx = drawFromShuffleBag(m_shaderShuffleBag, shaderPoolSize);
                        if (nextIdx != m_activeShaderIdx.load() && nextIdx >= 0) {
                            m_shaderFadeProgress.store(0.0f);
                            m_shaderFadeDuration.store(settings.shaderCrossfade);
                            m_targetShaderIdx.store(nextIdx);
                            
                            // Select Transition
                            std::vector<int> enabledTypes;
                            if (settings.enableLinearDissolve) enabledTypes.push_back(0);
                            if (settings.enableLumaWipe) enabledTypes.push_back(1);
                            if (settings.enableSweep) enabledTypes.push_back(2);
                            if (settings.enableCircleWipe) enabledTypes.push_back(3);
                            if (settings.enableLuminosityWipe) enabledTypes.push_back(4);
                            
                            if (!enabledTypes.empty()) {
                                std::uniform_int_distribution<int> transDist(0, (int)enabledTypes.size() - 1);
                                m_shaderTransitionType.store(enabledTypes[transDist(m_rng)]);
                            } else {
                                m_shaderTransitionType.store(0); // fallback linear
                            }
                            randomizeTransitionDirections(program);
                        }
                        m_shaderTimer.store(settings.shaderInterval + std::uniform_real_distribution<float>(-settings.shaderJitter, settings.shaderJitter)(m_rng));
                    }
                }
            }
        }

        // Fade Shaders
        if (m_shaderFadeProgress.load() < 1.0f) {
            if ((settings.shaderEnabled || settings.manualShaderOverride) && program->activeCueIndex.load() < 0) {
                m_shaderFadeProgress.store(m_shaderFadeProgress.load() + dt / std::max(0.001f, m_shaderFadeDuration.load()));
                if (m_shaderFadeProgress.load() >= 1.0f) {
                    m_shaderFadeProgress.store(1.0f);
                    m_activeShaderIdx.store(m_targetShaderIdx.load());
                    m_targetShaderIdx.store(-1);
                }
            }
        }
    }
}

void GenerativeEngineRuntime::triggerNextPalette(PatchProgram* program) {
    if (program->palettePool.empty()) return;
    int nextPalIdx = drawFromShuffleBag(m_paletteShuffleBag, program->palettePool.size());
    if (nextPalIdx != m_activePaletteIdx.load() && nextPalIdx >= 0) {
        startPaletteTransition(nextPalIdx, program);
    }
    const auto& settings = program->generativeSettings;
    m_paletteTimer.store(settings.paletteInterval + std::uniform_real_distribution<float>(-settings.paletteJitter, settings.paletteJitter)(m_rng));
}

void GenerativeEngineRuntime::triggerNextMotive(PatchProgram* program) {
    if (program->motivePool.empty()) return;
    int nextIdx = drawFromShuffleBag(m_motiveShuffleBag, program->motivePool.size());
    if (nextIdx != m_activeMotiveIdx.load() && nextIdx >= 0) {
        m_motiveFadeProgress.store(0.0f);
        m_motiveFadeDuration.store(program->generativeSettings.motiveCrossfade);
        m_targetMotiveIdx.store(nextIdx);
        
        const auto& act = program->motivePool[m_activeMotiveIdx.load()];
        m_currentAnchors[0] = act.velocity;
        m_currentAnchors[1] = act.complexity;
        m_currentAnchors[2] = act.scale;
        m_currentAnchors[3] = act.distortion;
        m_currentAnchors[4] = act.asymmetry;
        m_currentAnchors[5] = act.intensity;

        const auto& tgt = program->motivePool[m_targetMotiveIdx.load()];
        m_targetAnchors[0] = tgt.velocity;
        m_targetAnchors[1] = tgt.complexity;
        m_targetAnchors[2] = tgt.scale;
        m_targetAnchors[3] = tgt.distortion;
        m_targetAnchors[4] = tgt.asymmetry;
        m_targetAnchors[5] = tgt.intensity;
    }
    const auto& settings = program->generativeSettings;
    m_motiveTimer.store(settings.motiveInterval + std::uniform_real_distribution<float>(-settings.motiveJitter, settings.motiveJitter)(m_rng));
}

void GenerativeEngineRuntime::triggerNextShader(PatchProgram* program) {
    size_t shaderPoolSize = program->compiledCues.size();
    if (shaderPoolSize == 0) return;
    int nextIdx = drawFromShuffleBag(m_shaderShuffleBag, shaderPoolSize);
    if (nextIdx != m_activeShaderIdx.load() && nextIdx >= 0) {
        m_shaderFadeProgress.store(0.0f);
        m_shaderFadeDuration.store(program->generativeSettings.shaderCrossfade);
        m_targetShaderIdx.store(nextIdx);
        
        const auto& settings = program->generativeSettings;
        if (settings.manualTransitionType >= 0) {
            m_shaderTransitionType.store(settings.manualTransitionType);
        } else {
            std::vector<int> enabledTypes;
            if (settings.enableLinearDissolve) enabledTypes.push_back(0);
            if (settings.enableLumaWipe) enabledTypes.push_back(1);
            if (settings.enableSweep) enabledTypes.push_back(2);
            if (settings.enableCircleWipe) enabledTypes.push_back(3);
            if (settings.enableLuminosityWipe) enabledTypes.push_back(4);
            
            if (!enabledTypes.empty()) {
                std::uniform_int_distribution<int> transDist(0, (int)enabledTypes.size() - 1);
                m_shaderTransitionType.store(enabledTypes[transDist(m_rng)]);
            } else {
                m_shaderTransitionType.store(0);
            }
        }
        randomizeTransitionDirections(program);
    }
    const auto& settings = program->generativeSettings;
    m_shaderTimer.store(settings.shaderInterval + std::uniform_real_distribution<float>(-settings.shaderJitter, settings.shaderJitter)(m_rng));
}

void GenerativeEngineRuntime::startPaletteTransition(int targetIdx, PatchProgram* program) {
    if (targetIdx < 0 || targetIdx >= (int)program->palettePool.size()) return;
    if (targetIdx == m_activePaletteIdx.load()) return;

    m_paletteFadeProgress.store(0.0f);
    m_paletteFadeDuration.store(program->generativeSettings.paletteCrossfade);
    m_targetPaletteIdx.store(targetIdx);

    auto paletteA = program->palettePool[m_activePaletteIdx.load()];
    auto paletteB = program->palettePool[m_targetPaletteIdx.load()];

    auto enforceLoopingConstraints = [](std::vector<Generative::ColorStop>& stops) {
        if (stops.size() < 2) return;
        if (glm::distance(stops.front().color, stops.back().color) < 0.05f) {
            stops.front().position = 0.0f;
            stops.back().position = 1.0f;
            stops.back().color = stops.front().color;
        }
    };
    enforceLoopingConstraints(paletteA.stops);
    enforceLoopingConstraints(paletteB.stops);

    m_blendSize = std::min((int)std::max(paletteA.stops.size(), paletteB.stops.size()), 16);
    m_paddedA.resize(m_blendSize);
    m_paddedB.resize(m_blendSize);

    int N_A = (int)paletteA.stops.size();
    for (int i = 0; i < m_blendSize; i++) {
        if (N_A <= 1) {
            m_paddedA[i] = (N_A == 1) ? paletteA.stops[0] : Generative::ColorStop{};
        } else {
            double ratio = (double)i / (m_blendSize - 1);
            int srcIdx = (int)std::round(ratio * (N_A - 1));
            srcIdx = std::clamp(srcIdx, 0, N_A - 1);
            m_paddedA[i] = paletteA.stops[srcIdx];
        }
    }

    int N_B = (int)paletteB.stops.size();
    for (int i = 0; i < m_blendSize; i++) {
        if (N_B <= 1) {
            m_paddedB[i] = (N_B == 1) ? paletteB.stops[0] : Generative::ColorStop{};
        } else {
            double ratio = (double)i / (m_blendSize - 1);
            int srcIdx = (int)std::round(ratio * (N_B - 1));
            srcIdx = std::clamp(srcIdx, 0, N_B - 1);
            m_paddedB[i] = paletteB.stops[srcIdx];
        }
    }

    m_uboState.activeStops = m_blendSize;
}

} // namespace PixelMapper
