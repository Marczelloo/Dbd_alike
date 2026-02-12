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
    if (!uiSystem)
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

    // Rotate needle - the game handles timeout/fail logic
    m_state.needleAngle += m_state.rotationSpeed * deltaSeconds;
    if (m_state.needleAngle >= 360.0F)
    {
        m_state.needleAngle -= 360.0F;
    }
}

void SkillCheckWheel::Render()
{
    if (!m_ui)
    {
        return;
    }

    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const float centerX = static_cast<float>(screenWidth) / 2.0F;
    const float centerY = static_cast<float>(screenHeight) / 2.0F;

    if (m_state.active)
    {
        DrawWheel(centerX, centerY);
        DrawSuccessZoneArc(centerX, centerY, m_state.successZoneStart, m_state.successZoneEnd, false);
        
        if (m_state.bonusZoneEnd > m_state.bonusZoneStart)
        {
            DrawSuccessZoneArc(centerX, centerY, m_state.bonusZoneStart, m_state.bonusZoneEnd, true);
        }
        
        DrawNeedle(centerX, centerY, m_state.needleAngle);
    }

    if (m_state.hitFeedbackTime > 0.0F)
    {
        DrawHitFeedback(centerX, centerY);
    }
}

void SkillCheckWheel::TriggerSkillCheck(float successStart01, float successEnd01, float bonusWidth01)
{
    float successStart = successStart01 * 360.0F;
    float successEnd = successEnd01 * 360.0F;
    float successWidth = successEnd - successStart;
    
    const float reactionTime = successWidth / m_state.rotationSpeed;
    if (reactionTime < kMinReactionTime)
    {
        const float requiredWidth = kMinReactionTime * m_state.rotationSpeed;
        const float center = (successStart + successEnd) / 2.0F;
        successStart = std::max(0.0F, center - requiredWidth / 2.0F);
        successEnd = std::min(360.0F, center + requiredWidth / 2.0F);
        successWidth = successEnd - successStart;
    }
    
    m_state.active = true;
    m_state.needleAngle = 0.0F;
    m_state.successZoneStart = successStart;
    m_state.successZoneEnd = successEnd;
    
    const float bonusCenter = (successStart + successEnd) / 2.0F;
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
    // Input is handled by GameplaySystems - this wheel is visualization only
}

void SkillCheckWheel::DrawWheel(float centerX, float centerY)
{
    const float scale = m_ui->Scale();
    const float radius = m_state.wheelRadius * scale;
    const float innerRadius = radius * 0.65F;
    
    // Outer ring - dark background
    engine::ui::UiRect outerRect{
        centerX - radius,
        centerY - radius,
        radius * 2.0F,
        radius * 2.0F
    };
    m_ui->DrawRect(outerRect, glm::vec4{0.1F, 0.1F, 0.12F, 0.95F});
    
    // Inner circle - darker
    engine::ui::UiRect innerRect{
        centerX - innerRadius,
        centerY - innerRadius,
        innerRadius * 2.0F,
        innerRadius * 2.0F
    };
    m_ui->DrawRect(innerRect, glm::vec4{0.05F, 0.05F, 0.06F, 0.95F});
}

void SkillCheckWheel::DrawSuccessZoneArc(float centerX, float centerY, float startAngle, float endAngle, bool isBonus)
{
    const float scale = m_ui->Scale();
    const float radius = m_state.wheelRadius * scale;
    
    const float startRad = (startAngle - 90.0F) * 3.14159265F / 180.0F;
    const float endRad = (endAngle - 90.0F) * 3.14159265F / 180.0F;
    
    const float outerRadius = radius * 0.95F;
    const float innerRadius = radius * 0.65F;
    const float arcWidth = outerRadius - innerRadius;
    
    glm::vec4 color = isBonus 
        ? glm::vec4{0.2F, 0.9F, 0.3F, 0.95F}
        : glm::vec4{0.3F, 0.7F, 0.4F, 0.9F};
    
    // Draw arc as connected trapezoids
    const int segments = 16;
    const float angleStep = (endRad - startRad) / static_cast<float>(segments);
    
    for (int i = 0; i < segments; ++i)
    {
        float a1 = startRad + static_cast<float>(i) * angleStep;
        float a2 = startRad + static_cast<float>(i + 1) * angleStep;
        float midAngle = (a1 + a2) / 2.0F;
        
        // Calculate the center and size of this segment
        float midRadius = (outerRadius + innerRadius) / 2.0F;
        float arcLength = midRadius * std::abs(angleStep);
        
        // Make segment at least 8 pixels wide for visibility
        float segWidth = std::max(arcLength, 8.0F * scale);
        float segHeight = arcWidth;
        
        float segX = centerX + std::cos(midAngle) * midRadius - segWidth / 2.0F;
        float segY = centerY + std::sin(midAngle) * midRadius - segHeight / 2.0F;
        
        engine::ui::UiRect segRect{segX, segY, segWidth, segHeight};
        m_ui->DrawRect(segRect, color);
    }
}

void SkillCheckWheel::DrawNeedle(float centerX, float centerY, float angle)
{
    const float scale = m_ui->Scale();
    const float radius = m_state.wheelRadius * scale;
    
    // Needle angle starts from top
    const float angleRad = (angle - 90.0F) * 3.14159265F / 180.0F;
    const float needleLength = radius * 0.95F;
    
    // Needle tip position
    const float tipX = centerX + std::cos(angleRad) * needleLength;
    const float tipY = centerY + std::sin(angleRad) * needleLength;
    
    // Draw needle as a thick line (rectangle from center to tip)
    const float needleWidth = 8.0F * scale;
    
    // Simple approach: draw a small rect at tip and a line
    engine::ui::UiRect needleBody{
        centerX - needleWidth / 2.0F,
        centerY,
        needleWidth,
        needleLength
    };
    m_ui->DrawRect(needleBody, glm::vec4{0.95F, 0.95F, 0.95F, 1.0F});
    
    // Red circle at tip
    const float tipSize = 18.0F * scale;
    engine::ui::UiRect tipRect{
        tipX - tipSize / 2.0F,
        tipY - tipSize / 2.0F,
        tipSize,
        tipSize
    };
    m_ui->DrawRect(tipRect, glm::vec4{0.95F, 0.2F, 0.2F, 1.0F});
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
        textColor = glm::vec4{0.2F, 0.95F, 0.4F, alpha};
    }
    else if (m_state.hitSuccess)
    {
        feedbackText = "GOOD";
        textColor = glm::vec4{0.4F, 0.85F, 0.5F, alpha};
    }
    else
    {
        feedbackText = "MISS";
        textColor = glm::vec4{0.95F, 0.25F, 0.25F, alpha};
    }
    
    const float textY = centerY + m_state.wheelRadius * scale + 50.0F * scale;
    m_ui->DrawTextLabel(centerX - 50.0F * scale, textY, feedbackText, textColor, 1.8F * scale);
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
