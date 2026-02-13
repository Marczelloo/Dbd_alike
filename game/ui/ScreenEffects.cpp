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
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    intensity = std::clamp(intensity, 0.0F, 1.0F);
    
    if (intensity < 0.01F)
    {
        return;
    }
    
    // Smooth oval vignette using distance-based alpha on a grid
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    const float centerX = w * 0.5F;
    const float centerY = h * 0.5F;
    const float maxDist = std::sqrt(centerX * centerX + centerY * centerY);
    
    // Pulse effect for chase
    const float pulseBoost = pulse > 0.5F ? std::sin(m_internalPulseTime * 4.0F) * 0.15F : 0.0F;
    const float baseAlpha = (intensity + pulseBoost) * 0.75F;
    
    // Vignette parameters - how far from center it starts to fade
    const float vignetteStart = 0.35F;  // 0 = center, 1 = corner
    const float vignetteEnd = 1.15F;    // Full intensity at this distance
    
    // Draw grid of rectangles with alpha based on elliptical distance
    constexpr int kGridX = 96;
    constexpr int kGridY = 72;
    const float cellW = w / static_cast<float>(kGridX);
    const float cellH = h / static_cast<float>(kGridY);
    
    for (int iy = 0; iy < kGridY; ++iy)
    {
        for (int ix = 0; ix < kGridX; ++ix)
        {
            // Center of this cell
            const float cx = (static_cast<float>(ix) + 0.5F) * cellW;
            const float cy = (static_cast<float>(iy) + 0.5F) * cellH;
            
            // Normalized elliptical distance from center (0 = center, 1 = edge)
            const float dx = (cx - centerX) / centerX;
            const float dy = (cy - centerY) / centerY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            
            // Only draw cells in the vignette zone
            if (dist < vignetteStart)
            {
                continue;
            }
            
            // Calculate alpha based on distance
            float alpha = std::clamp((dist - vignetteStart) / (vignetteEnd - vignetteStart), 0.0F, 1.0F);
            alpha = std::pow(alpha, 1.2F);  // Smooth curve
            alpha *= baseAlpha;
            
            if (alpha < 0.01F)
            {
                continue;
            }
            
            engine::ui::UiRect rect{cx - cellW * 0.5F, cy - cellH * 0.5F, cellW, cellH};
            glm::vec4 color(r, g, b, alpha);
            m_ui->DrawRect(rect, color);
        }
    }
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
