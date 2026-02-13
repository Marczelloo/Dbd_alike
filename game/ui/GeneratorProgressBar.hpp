#pragma once

#include <string>

namespace engine::ui
{
class UiSystem;
}

namespace game::ui
{

struct GeneratorProgressState
{
    bool isActive = false;
    bool isRepairing = false;
    float progress = 0.0F;
    std::string generatorName = "Generator";
    int generatorsCompleted = 0;
    int generatorsTotal = 5;
};

class GeneratorProgressBar
{
public:
    GeneratorProgressBar() = default;
    ~GeneratorProgressBar() = default;

    bool Initialize(engine::ui::UiSystem* uiSystem);
    void Shutdown();

    void Render(const GeneratorProgressState& state);
    
    void SetBarWidth(float width) { m_barWidth = width; }
    void SetBarHeight(float height) { m_barHeight = height; }
    void SetBottomOffset(float offset) { m_bottomOffset = offset; }

private:
    void DrawProgressBar(float progress01, float x, float y, float width, float height);
    void DrawGeneratorIcon(float x, float y, float size);
    void DrawObjectivePanel(int completed, int total, float x, float y);

    engine::ui::UiSystem* m_ui = nullptr;
    
    float m_barWidth = 300.0F;
    float m_barHeight = 24.0F;
    float m_bottomOffset = 120.0F;
};

}
