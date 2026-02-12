#include "SkillCheckWheel.hpp"

#include <cmath>

#include "engine/ui/UiSystem.hpp"
#include "engine/render/Renderer.hpp"
#include <GLFW/glfw3.h>

namespace game::ui
{

SkillCheckWheel::SkillCheckWheel()
{
}

bool SkillCheckWheel::Initialize(engine::ui::UiSystem* uiSystem, engine::render::Renderer* renderer)
{
    if (!uiSystem || !renderer)
    {
        return false;
    }
    m_ui = uiSystem;
    m_renderer = renderer;
    m_state = SkillCheckState{};
    return true;
}

void SkillCheckWheel::Shutdown()
{
    m_ui = nullptr;
    m_renderer = nullptr;
}

void SkillCheckWheel::Update(float deltaSeconds)
{
    if (!m_state.active)
    {
        if (m_state.hitFeedbackTime > 0.0F)
        {
            m_state.hitFeedbackTime -= deltaSeconds;
        }
        return;
    }

    m_state.needleAngle += m_state.rotationSpeed * deltaSeconds;
    if (m_state.needleAngle >= 360.0F)
    {
        m_state.needleAngle -= 360.0F;
    }
}

void SkillCheckWheel::Render()
{
    if (!m_ui || !m_renderer || !m_state.active)
    {
        if (m_state.hitFeedbackTime > 0.0F && m_ui)
        {
            const int screenWidth = m_ui->ScreenWidth();
            const int screenHeight = m_ui->ScreenHeight();
            const float centerX = static_cast<float>(screenWidth) / 2.0F;
            const float centerY = static_cast<float>(screenHeight) / 2.0F;
            DrawHitFeedback(centerX, centerY);
        }
        return;
    }

    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const float scale = m_ui->Scale();
    const float centerX = static_cast<float>(screenWidth) / 2.0F;
    const float centerY = static_cast<float>(screenHeight) / 2.0F;

    DrawWheel(centerX, centerY);
    DrawSuccessZoneArc(centerX, centerY, m_state.successZoneStart, m_state.successZoneEnd, false);
    
    if (m_state.bonusZoneEnd > m_state.bonusZoneStart)
    {
        DrawSuccessZoneArc(centerX, centerY, m_state.bonusZoneStart, m_state.bonusZoneEnd, true);
    }
    
    DrawNeedle(centerX, centerY, m_state.needleAngle);

    if (m_state.hitFeedbackTime > 0.0F)
    {
        DrawHitFeedback(centerX, centerY);
    }
}

void SkillCheckWheel::TriggerSkillCheck(float successStart01, float successEnd01, float bonusWidth01)
{
    m_state.active = true;
    m_state.needleAngle = 0.0F;
    m_state.successZoneStart = successStart01 * 360.0F;
    m_state.successZoneEnd = successEnd01 * 360.0F;
    
    const float successWidth = m_state.successZoneEnd - m_state.successZoneStart;
    const float bonusCenter = (m_state.successZoneStart + m_state.successZoneEnd) / 2.0F;
    const float bonusHalfWidth = (bonusWidth01 * successWidth) / 2.0F;
    
    m_state.bonusZoneStart = bonusCenter - bonusHalfWidth;
    m_state.bonusZoneEnd = bonusCenter + bonusHalfWidth;
    
    m_state.hitSuccess = false;
    m_state.hitBonus = false;
    m_state.hitFailed = false;
    m_state.hitFeedbackTime = 0.0F;
}

void SkillCheckWheel::HandleInput()
{
    if (!m_state.active)
    {
        return;
    }

    static bool spaceWasPressed = false;
    bool spaceIsPressed = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_SPACE) == GLFW_PRESS;
    
    if (spaceIsPressed && !spaceWasPressed)
    {
        float needle = m_state.needleAngle;
        while (needle >= 360.0F) needle -= 360.0F;
        while (needle < 0.0F) needle += 360.0F;
        
        bool inBonus = (needle >= m_state.bonusZoneStart && needle <= m_state.bonusZoneEnd);
        bool inSuccess = (needle >= m_state.successZoneStart && needle <= m_state.successZoneEnd);
        
        if (inBonus)
        {
            EndSkillCheck(true, true);
        }
        else if (inSuccess)
        {
            EndSkillCheck(true, false);
        }
        else
        {
            EndSkillCheck(false, false);
        }
    }
    
    spaceWasPressed = spaceIsPressed;
}

void SkillCheckWheel::DrawWheel(float centerX, float centerY)
{
    const float scale = m_ui->Scale();
    const float radius = m_state.wheelRadius * scale;
    
    for (int i = 0; i < kCircleSegments; ++i)
    {
        float a1 = static_cast<float>(i) / kCircleSegments * 6.28318F;
        float a2 = static_cast<float>(i + 1) / kCircleSegments * 6.28318F;
        
        glm::vec3 p1{centerX + std::cos(a1) * radius, centerY + std::sin(a1) * radius, 0.0F};
        glm::vec3 p2{centerX + std::cos(a2) * radius, centerY + std::sin(a2) * radius, 0.0F};
        
        m_renderer->DrawOverlayLine(p1, p2, glm::vec3{0.3F, 0.3F, 0.35F});
    }
    
    const float innerRadius = radius * 0.7F;
    for (int i = 0; i < kCircleSegments; ++i)
    {
        float a1 = static_cast<float>(i) / kCircleSegments * 6.28318F;
        float a2 = static_cast<float>(i + 1) / kCircleSegments * 6.28318F;
        
        glm::vec3 p1{centerX + std::cos(a1) * innerRadius, centerY + std::sin(a1) * innerRadius, 0.0F};
        glm::vec3 p2{centerX + std::cos(a2) * innerRadius, centerY + std::sin(a2) * innerRadius, 0.0F};
        
        m_renderer->DrawOverlayLine(p1, p2, glm::vec3{0.2F, 0.2F, 0.22F});
    }
}

void SkillCheckWheel::DrawSuccessZoneArc(float centerX, float centerY, float startAngle, float endAngle, bool isBonus)
{
    const float scale = m_ui->Scale();
    const float outerRadius = m_state.wheelRadius * scale * 0.95F;
    const float innerRadius = m_state.wheelRadius * scale * 0.7F;
    
    glm::vec3 color = isBonus ? glm::vec3{0.2F, 0.9F, 0.3F} : glm::vec3{0.3F, 0.7F, 0.4F};
    
    const float startRad = startAngle * 3.14159265F / 180.0F - 1.5708F;
    const float endRad = endAngle * 3.14159265F / 180.0F - 1.5708F;
    const int segments = 16;
    const float angleStep = (endRad - startRad) / static_cast<float>(segments);
    
    for (int i = 0; i < segments; ++i)
    {
        float a1 = startRad + static_cast<float>(i) * angleStep;
        float a2 = startRad + static_cast<float>(i + 1) * angleStep;
        
        glm::vec3 outer1{centerX + std::cos(a1) * outerRadius, centerY + std::sin(a1) * outerRadius, 0.0F};
        glm::vec3 outer2{centerX + std::cos(a2) * outerRadius, centerY + std::sin(a2) * outerRadius, 0.0F};
        
        m_renderer->DrawOverlayLine(outer1, outer2, color);
        
        glm::vec3 inner1{centerX + std::cos(a1) * innerRadius, centerY + std::sin(a1) * innerRadius, 0.0F};
        glm::vec3 inner2{centerX + std::cos(a2) * innerRadius, centerY + std::sin(a2) * innerRadius, 0.0F};
        
        m_renderer->DrawOverlayLine(inner1, inner2, color);
        
        m_renderer->DrawOverlayLine(outer1, inner1, color);
    }
}

void SkillCheckWheel::DrawNeedle(float centerX, float centerY, float angle)
{
    const float scale = m_ui->Scale();
    const float length = m_state.wheelRadius * scale;
    const float angleRad = angle * 3.14159265F / 180.0F - 1.5708F;
    
    glm::vec3 start{centerX, centerY, 0.0F};
    glm::vec3 end{centerX + std::cos(angleRad) * length, centerY + std::sin(angleRad) * length, 0.0F};
    
    m_renderer->DrawOverlayLine(start, end, glm::vec3{1.0F, 1.0F, 1.0F});
    
    const float tipRadius = 10.0F * scale;
    for (int i = 0; i < 8; ++i)
    {
        float a1 = static_cast<float>(i) / 8.0F * 6.28318F;
        float a2 = static_cast<float>(i + 1) / 8.0F * 6.28318F;
        
        glm::vec3 p1{end.x + std::cos(a1) * tipRadius, end.y + std::sin(a1) * tipRadius, 0.0F};
        glm::vec3 p2{end.x + std::cos(a2) * tipRadius, end.y + std::sin(a2) * tipRadius, 0.0F};
        
        m_renderer->DrawOverlayLine(p1, p2, glm::vec3{1.0F, 0.3F, 0.3F});
    }
}

void SkillCheckWheel::DrawHitFeedback(float centerX, float centerY)
{
    if (!m_ui) return;
    
    const float scale = m_ui->Scale();
    const float alpha = std::min(1.0F, m_state.hitFeedbackTime / kFeedbackDuration);
    
    std::string feedbackText;
    glm::vec4 textColor;
    
    if (m_state.hitBonus)
    {
        feedbackText = "GREAT!";
        textColor = glm::vec4{0.2F, 0.9F, 0.4F, alpha};
    }
    else if (m_state.hitSuccess)
    {
        feedbackText = "GOOD";
        textColor = glm::vec4{0.4F, 0.8F, 0.5F, alpha};
    }
    else
    {
        feedbackText = "MISS";
        textColor = glm::vec4{0.9F, 0.2F, 0.2F, alpha};
    }
    
    const float textY = centerY + m_state.wheelRadius * scale + 50.0F * scale;
    m_ui->DrawTextLabel(centerX - 40.0F * scale, textY, feedbackText, textColor, 1.8F * scale);
}

void SkillCheckWheel::EndSkillCheck(bool success, bool bonus)
{
    m_state.active = false;
    m_state.hitSuccess = success;
    m_state.hitBonus = bonus;
    m_state.hitFailed = !success;
    m_state.hitFeedbackTime = kFeedbackDuration;
    
    if (bonus && m_onBonus)
    {
        m_onBonus();
    }
    else if (success && m_onSuccess)
    {
        m_onSuccess();
    }
    else if (!success && m_onFail)
    {
        m_onFail();
    }
}

}
