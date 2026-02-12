#pragma once

#include <cstdint>

namespace engine::ui
{
class UiSystem;
}

namespace engine::render
{
class Renderer;
}

namespace game::ui
{

struct SkillCheckState
{
    bool active = false;
    float needleAngle = 0.0F;
    float successZoneStart = 0.0F;
    float successZoneEnd = 0.0F;
    float bonusZoneStart = 0.0F;
    float bonusZoneEnd = 0.0F;
    float rotationSpeed = 200.0F;
    float wheelRadius = 120.0F;
    
    bool hitSuccess = false;
    bool hitBonus = false;
    bool hitFailed = false;
    float hitFeedbackTime = 0.0F;
};

class SkillCheckWheel
{
public:
    SkillCheckWheel();
    ~SkillCheckWheel() = default;

    bool Initialize(engine::ui::UiSystem* uiSystem, engine::render::Renderer* renderer);
    void Shutdown();

    void Update(float deltaSeconds);
    void Render();

    void TriggerSkillCheck(float successStart01, float successEnd01, float bonusWidth01 = 0.15F);
    void HandleInput();
    
    [[nodiscard]] bool IsActive() const { return m_state.active; }
    [[nodiscard]] bool WasSuccess() const { return m_state.hitSuccess; }
    [[nodiscard]] bool WasBonus() const { return m_state.hitBonus; }
    [[nodiscard]] bool WasFailed() const { return m_state.hitFailed; }
    
    void SetCallbackOnSuccess(void (*callback)()) { m_onSuccess = callback; }
    void SetCallbackOnBonus(void (*callback)()) { m_onBonus = callback; }
    void SetCallbackOnFail(void (*callback)()) { m_onFail = callback; }

    SkillCheckState& GetState() { return m_state; }
    const SkillCheckState& GetState() const { return m_state; }

private:
    void DrawWheel(float centerX, float centerY);
    void DrawSuccessZoneArc(float centerX, float centerY, float startAngle, float endAngle, bool isBonus);
    void DrawNeedle(float centerX, float centerY, float angle);
    void DrawHitFeedback(float centerX, float centerY);
    void EndSkillCheck(bool success, bool bonus);

    engine::ui::UiSystem* m_ui = nullptr;
    engine::render::Renderer* m_renderer = nullptr;
    SkillCheckState m_state;
    
    void (*m_onSuccess)() = nullptr;
    void (*m_onBonus)() = nullptr;
    void (*m_onFail)() = nullptr;

    static constexpr float kFeedbackDuration = 1.0F;
    static constexpr float kMinReactionTime = 0.5F;
    static constexpr float kSuccessZoneMargin = 0.08F;
    static constexpr int kCircleSegments = 64;
};

}
