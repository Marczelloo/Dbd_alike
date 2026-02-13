#include "GeneratorProgressBar.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "engine/ui/UiSystem.hpp"

namespace game::ui
{

bool GeneratorProgressBar::Initialize(engine::ui::UiSystem* uiSystem)
{
    if (!uiSystem)
    {
        return false;
    }
    m_ui = uiSystem;
    return true;
}

void GeneratorProgressBar::Shutdown()
{
    m_ui = nullptr;
}

void GeneratorProgressBar::Render(const GeneratorProgressState& state)
{
    if (!m_ui || !state.isActive)
    {
        return;
    }

    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    
    const float barWidth = m_barWidth * scale;
    const float barHeight = m_barHeight * scale;
    const float bottomOffset = m_bottomOffset * scale;
    
    const float barX = (screenWidth - barWidth) / 2.0F;
    const float barY = screenHeight - bottomOffset - barHeight;
    
    if (state.isRepairing)
    {
        DrawGeneratorIcon(barX - 40.0F * scale, barY, 32.0F * scale);
        DrawProgressBar(state.progress, barX, barY, barWidth, barHeight);
        
        std::ostringstream oss;
        oss << "Repairing... " << static_cast<int>(state.progress * 100.0F) << "%";
        m_ui->DrawTextLabel(barX, barY - 25.0F * scale, oss.str(), theme.colorTextMuted, 0.9F * scale);
    }
    
    DrawObjectivePanel(state.generatorsCompleted, state.generatorsTotal, screenWidth - 180.0F * scale, 80.0F * scale);
}

void GeneratorProgressBar::DrawProgressBar(float progress01, float x, float y, float width, float height)
{
    const auto& theme = m_ui->Theme();
    
    engine::ui::UiRect bgRect{x, y, width, height};
    glm::vec4 bgColor = theme.colorPanel;
    bgColor.a = 0.85F;
    m_ui->DrawRect(bgRect, bgColor);
    m_ui->DrawRectOutline(bgRect, 2.0F, theme.colorPanelBorder);
    
    const float fillWidth = width * std::clamp(progress01, 0.0F, 1.0F);
    if (fillWidth > 0.0F)
    {
        engine::ui::UiRect fillRect{x, y, fillWidth, height};
        glm::vec4 fillColor = theme.colorAccent;
        fillColor.a = 0.9F;
        m_ui->DrawRect(fillRect, fillColor);
    }
}

void GeneratorProgressBar::DrawGeneratorIcon(float x, float y, float size)
{
    const auto& theme = m_ui->Theme();
    
    engine::ui::UiRect iconRect{x, y, size, size};
    glm::vec4 iconColor = theme.colorSuccess;
    iconColor.a = 0.8F;
    m_ui->DrawRect(iconRect, iconColor);
    
    m_ui->DrawTextLabel(x + 4.0F, y + 4.0F, "G", theme.colorText, 0.8F);
}

void GeneratorProgressBar::DrawObjectivePanel(int completed, int total, float x, float y)
{
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    const float panelWidth = 160.0F * scale;
    const float panelHeight = 50.0F * scale;
    
    engine::ui::UiRect panelRect{x, y, panelWidth, panelHeight};
    glm::vec4 panelColor = theme.colorPanel;
    panelColor.a = 0.75F;
    m_ui->DrawRect(panelRect, panelColor);
    m_ui->DrawRectOutline(panelRect, 1.5F, theme.colorPanelBorder);
    
    m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, "OBJECTIVE", theme.colorTextMuted, 0.7F * scale);
    
    std::ostringstream oss;
    oss << "Generators: " << completed << "/" << total;
    m_ui->DrawTextLabel(x + 10.0F * scale, y + 26.0F * scale, oss.str(), theme.colorText, 0.85F * scale);
    
    const float iconSize = 12.0F * scale;
    const float startX = x + 10.0F * scale;
    const float iconY = y + 38.0F * scale;
    
    for (int i = 0; i < total; ++i)
    {
        engine::ui::UiRect iconRect{startX + i * (iconSize + 4.0F * scale), iconY, iconSize, iconSize};
        glm::vec4 color = (i < completed) ? theme.colorSuccess : theme.colorPanel;
        color.a = 0.8F;
        m_ui->DrawRect(iconRect, color);
        m_ui->DrawRectOutline(iconRect, 1.0F, theme.colorPanelBorder);
    }
}

}
