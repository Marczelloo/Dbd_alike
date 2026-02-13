#pragma once

#include <cstdint>

namespace engine::ui
{
class UiSystem;
}

namespace game::ui
{

struct ScreenEffectsState
{
    bool terrorRadiusActive = false;
    float terrorRadiusIntensity = 0.0F;
    bool chaseActive = false;
    float chasePulseTime = 0.0F;
    
    bool lowHealthActive = false;
    float lowHealthIntensity = 0.0F;
    
    bool damageFlash = false;
    float damageFlashTime = 0.0F;
};

class ScreenEffects
{
public:
    ScreenEffects() = default;
    ~ScreenEffects() = default;

    bool Initialize(engine::ui::UiSystem* uiSystem);
    void Shutdown();

    void Update(float deltaSeconds);
    void Render(const ScreenEffectsState& state);
    
    void TriggerDamageFlash();
    
    void SetVignetteBaseIntensity(float intensity) { m_baseIntensity = intensity; }
    void SetChasePulseSpeed(float speed) { m_chasePulseSpeed = speed; }

private:
    void DrawVignette(float intensity, float r, float g, float b, float pulse = 0.0F);
    void DrawDamageFlash(float alpha);

    engine::ui::UiSystem* m_ui = nullptr;
    
    float m_baseIntensity = 0.4F;
    float m_chasePulseSpeed = 3.0F;
    float m_internalPulseTime = 0.0F;
    float m_damageFlashDuration = 0.15F;
};

}
