#include "ScreenEffects.hpp"

#include <cmath>
#include <algorithm>

#include "engine/ui/UiSystem.hpp"

namespace game::ui
{

bool ScreenEffects::Initialize(engine::ui::UiSystem* uiSystem)
{
    if (!uiSystem)
    {
        return false;
    }
    m_ui = uiSystem;
    m_internalPulseTime = 0.0F;
    return true;
}

void ScreenEffects::Shutdown()
{
    m_ui = nullptr;
}

void ScreenEffects::Update(float deltaSeconds)
{
    m_internalPulseTime += deltaSeconds;
}

void ScreenEffects::Render(const ScreenEffectsState& state)
{
    if (!m_ui)
    {
        return;
    }

    if (state.damageFlash && state.damageFlashTime > 0.0F)
    {
        const float alpha = std::clamp(state.damageFlashTime / m_damageFlashDuration, 0.0F, 1.0F);
        DrawDamageFlash(alpha);
    }

    // Red vignette only during chase (not terror radius)
    if (state.chaseActive)
    {
        const float pulse = std::sin(m_internalPulseTime * m_chasePulseSpeed * 2.0F) * 0.15F;
        const float intensity = 0.6F;
        
        DrawVignette(
            intensity * m_baseIntensity + pulse,
            0.7F, 0.12F, 0.08F,
            1.0F
        );
    }

    if (state.lowHealthActive)
    {
        const float lowPulse = std::sin(m_internalPulseTime * 1.5F) * 0.5F + 0.5F;
        DrawVignette(
            state.lowHealthIntensity * 0.25F * lowPulse,
            0.7F, 0.1F, 0.1F,
            0.0F
        );
    }
}

void ScreenEffects::TriggerDamageFlash()
{
    // Note: The actual flash rendering is handled by DrawDamageFlash()
    // This method exists as a public API placeholder for triggering the effect
    // In a full implementation, this would set internal state to start the flash
    // For now, the damage flash is driven through ScreenEffectsState.damageFlash
}

void ScreenEffects::DrawVignette(float intensity, float r, float g, float b, float pulse)
{
    intensity = std::clamp(intensity, 0.0F, 1.0F);

    if (intensity < 0.01F)
    {
        return;
    }

    // Pulse effect for chase
    const float pulseBoost = pulse > 0.5F ? std::sin(m_internalPulseTime * 4.0F) * 0.15F : 0.0F;
    const float baseAlpha = std::clamp((intensity + pulseBoost) * 0.75F, 0.0F, 1.0F);
    m_ui->DrawFullscreenVignette(glm::vec4(r, g, b, baseAlpha));
}

void ScreenEffects::DrawDamageFlash(float alpha)
{
    if (alpha <= 0.0F)
    {
        return;
    }
    
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    engine::ui::UiRect fullScreen{0.0F, 0.0F, static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
    glm::vec4 flashColor(1.0F, 0.2F, 0.2F, alpha * 0.4F);
    m_ui->DrawRect(fullScreen, flashColor);
}

}
