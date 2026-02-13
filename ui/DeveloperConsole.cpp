#include "ui/DeveloperConsole.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "engine/platform/Window.hpp"
#include "game/gameplay/GameplaySystems.hpp"

#if BUILD_WITH_IMGUI
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#endif

namespace ui
{
#if BUILD_WITH_IMGUI
namespace
{
// Colors for graphical perk HUD
namespace PerkHudColors
{
    constexpr ImU32 Background = IM_COL32(8, 8, 10, 200);
    constexpr ImU32 SlotReady = IM_COL32(30, 35, 45, 240);
    constexpr ImU32 SlotCooldown = IM_COL32(18, 18, 22, 200);
    constexpr ImU32 SlotActive = IM_COL32(80, 55, 25, 250);
    constexpr ImU32 CooldownRing = IM_COL32(170, 175, 185, 255);
    constexpr ImU32 CooldownBg = IM_COL32(30, 30, 35, 150);
    constexpr ImU32 BorderReady = IM_COL32(65, 75, 90, 255);
    constexpr ImU32 BorderCooldown = IM_COL32(35, 35, 40, 180);
    constexpr ImU32 ActiveGlow = IM_COL32(255, 200, 80, 150);
}

// Perk icon pattern types
enum class PerkPattern
{
    Lightning,      // Sprint, speed
    Heart,          // Healing, self-care
    Shield,         // Protection, iron will
    ArrowUp,        // Dead hard, escape
    Fist,           // Iron grasp, strength
    Eye,            // Whispers, detection
    Star,           // Bamboozle, tricks
    Hammer,         // Brutal, breaking
    Default
};

// Determine perk pattern from id
PerkPattern GetPerkPattern(const std::string& perkId)
{
    if (perkId.find("sprint") != std::string::npos || perkId.find("adrenaline") != std::string::npos) return PerkPattern::Lightning;
    if (perkId.find("heal") != std::string::npos || perkId.find("self_care") != std::string::npos || perkId.find("sloppy") != std::string::npos) return PerkPattern::Heart;
    if (perkId.find("iron_will") != std::string::npos || perkId.find("resilience") != std::string::npos) return PerkPattern::Shield;
    if (perkId.find("dead_hard") != std::string::npos) return PerkPattern::ArrowUp;
    if (perkId.find("iron_grasp") != std::string::npos || perkId.find("enduring") != std::string::npos) return PerkPattern::Fist;
    if (perkId.find("whispers") != std::string::npos || perkId.find("terrifying") != std::string::npos) return PerkPattern::Eye;
    if (perkId.find("bamboozle") != std::string::npos) return PerkPattern::Star;
    if (perkId.find("brutal") != std::string::npos) return PerkPattern::Hammer;
    return PerkPattern::Default;
}

// Get colors for perk based on id
void GetPerkColors(const std::string& perkId, ImU32& primary, ImU32& secondary, ImU32& accent)
{
    // Sprint/speed - blue/cyan
    if (perkId.find("sprint") != std::string::npos || perkId.find("adrenaline") != std::string::npos)
    {
        primary = IM_COL32(40, 130, 200, 255);
        secondary = IM_COL32(80, 180, 240, 255);
        accent = IM_COL32(150, 220, 255, 255);
        return;
    }
    // Healing - red/pink
    if (perkId.find("heal") != std::string::npos || perkId.find("self_care") != std::string::npos || perkId.find("sloppy") != std::string::npos)
    {
        primary = IM_COL32(180, 50, 60, 255);
        secondary = IM_COL32(220, 90, 100, 255);
        accent = IM_COL32(255, 150, 160, 255);
        return;
    }
    // Iron will - gray/steel
    if (perkId.find("iron_will") != std::string::npos)
    {
        primary = IM_COL32(90, 100, 120, 255);
        secondary = IM_COL32(130, 140, 160, 255);
        accent = IM_COL32(180, 190, 210, 255);
        return;
    }
    // Dead hard - green
    if (perkId.find("dead_hard") != std::string::npos)
    {
        primary = IM_COL32(40, 160, 80, 255);
        secondary = IM_COL32(70, 200, 110, 255);
        accent = IM_COL32(140, 240, 170, 255);
        return;
    }
    // Resilience - teal
    if (perkId.find("resilience") != std::string::npos)
    {
        primary = IM_COL32(50, 140, 130, 255);
        secondary = IM_COL32(80, 180, 170, 255);
        accent = IM_COL32(140, 220, 210, 255);
        return;
    }
    // Killer perks - purple/red
    if (perkId.find("iron_grasp") != std::string::npos || perkId.find("enduring") != std::string::npos)
    {
        primary = IM_COL32(150, 60, 60, 255);
        secondary = IM_COL32(190, 90, 90, 255);
        accent = IM_COL32(240, 140, 140, 255);
        return;
    }
    if (perkId.find("whispers") != std::string::npos || perkId.find("terrifying") != std::string::npos)
    {
        primary = IM_COL32(110, 50, 150, 255);
        secondary = IM_COL32(150, 80, 200, 255);
        accent = IM_COL32(200, 140, 250, 255);
        return;
    }
    if (perkId.find("bamboozle") != std::string::npos)
    {
        primary = IM_COL32(180, 130, 40, 255);
        secondary = IM_COL32(220, 170, 70, 255);
        accent = IM_COL32(255, 210, 120, 255);
        return;
    }
    if (perkId.find("brutal") != std::string::npos)
    {
        primary = IM_COL32(130, 50, 50, 255);
        secondary = IM_COL32(180, 70, 70, 255);
        accent = IM_COL32(230, 120, 120, 255);
        return;
    }
    // Default
    primary = IM_COL32(70, 100, 150, 255);
    secondary = IM_COL32(100, 140, 190, 255);
    accent = IM_COL32(160, 200, 240, 255);
}

// Draw procedural perk icon pattern
void DrawPerkPattern(ImDrawList* drawList, const ImVec2& center, float size, PerkPattern pattern, ImU32 primary, ImU32 secondary, ImU32 accent, float animPhase, bool isActive)
{
    const float halfSize = size * 0.5F;
    const float quarterSize = size * 0.25F;

    switch (pattern)
    {
        case PerkPattern::Lightning:
        {
            // Lightning bolt icon
            const float pulse = isActive ? 0.5F + 0.5F * std::sin(animPhase * 8.0F) : 0.0F;
            const ImU32 glowColor = isActive ? 
                IM_COL32(150 + static_cast<int>(105 * pulse), static_cast<int>(220 * (1 - pulse * 0.3F)), 255, static_cast<int>(80 + 100 * pulse)) :
                primary;
            
            // Main bolt
            ImVec2 bolt[] = {
                ImVec2(center.x + quarterSize * 0.3F, center.y - halfSize * 0.8F),
                ImVec2(center.x - quarterSize * 0.5F, center.y),
                ImVec2(center.x + quarterSize * 0.1F, center.y),
                ImVec2(center.x - quarterSize * 0.3F, center.y + halfSize * 0.8F),
            };
            drawList->AddPolyline(bolt, 4, glowColor, false, 3.0F);
            drawList->AddPolyline(bolt, 4, secondary, false, 2.0F);
            break;
        }
        case PerkPattern::Heart:
        {
            // Heart shape
            const float beat = isActive ? 1.0F + 0.15F * std::sin(animPhase * 10.0F) : 1.0F;
            const float r = quarterSize * 0.8F * beat;
            
            // Two circles for top of heart
            drawList->AddCircleFilled(ImVec2(center.x - r * 0.55F, center.y - r * 0.3F), r * 0.7F, primary, 16);
            drawList->AddCircleFilled(ImVec2(center.x + r * 0.55F, center.y - r * 0.3F), r * 0.7F, primary, 16);
            // Bottom triangle
            ImVec2 heartBot[] = {
                ImVec2(center.x - r * 1.1F, center.y - r * 0.1F),
                ImVec2(center.x + r * 1.1F, center.y - r * 0.1F),
                ImVec2(center.x, center.y + r * 1.0F)
            };
            drawList->AddConvexPolyFilled(heartBot, 3, primary);
            
            if (isActive)
            {
                // Glow effect
                const ImU32 glow = IM_COL32(255, 150, 160, 80);
                drawList->AddCircleFilled(center, r * 1.5F, glow, 24);
            }
            break;
        }
        case PerkPattern::Shield:
        {
            // Shield shape
            ImVec2 shield[] = {
                ImVec2(center.x, center.y - halfSize * 0.7F),
                ImVec2(center.x + halfSize * 0.6F, center.y - halfSize * 0.3F),
                ImVec2(center.x + halfSize * 0.5F, center.y + halfSize * 0.4F),
                ImVec2(center.x, center.y + halfSize * 0.7F),
                ImVec2(center.x - halfSize * 0.5F, center.y + halfSize * 0.4F),
                ImVec2(center.x - halfSize * 0.6F, center.y - halfSize * 0.3F),
            };
            drawList->AddConvexPolyFilled(shield, 6, primary);
            drawList->AddPolyline(shield, 6, secondary, true, 1.5F);
            
            // Inner cross
            drawList->AddLine(ImVec2(center.x, center.y - quarterSize * 0.5F), ImVec2(center.x, center.y + quarterSize * 0.6F), accent, 2.0F);
            drawList->AddLine(ImVec2(center.x - quarterSize * 0.5F, center.y), ImVec2(center.x + quarterSize * 0.5F, center.y), accent, 2.0F);
            break;
        }
        case PerkPattern::ArrowUp:
        {
            // Arrow pointing upward (escape/dodge)
            const float bounce = isActive ? -quarterSize * 0.3F * std::sin(animPhase * 12.0F) : 0.0F;
            const ImVec2 tip(center.x, center.y - halfSize * 0.7F + bounce);
            
            // Arrow head
            ImVec2 arrowHead[] = {
                tip,
                ImVec2(center.x - halfSize * 0.5F, center.y - quarterSize * 0.3F + bounce),
                ImVec2(center.x - quarterSize * 0.2F, center.y - quarterSize * 0.3F + bounce),
                ImVec2(center.x - quarterSize * 0.2F, center.y + halfSize * 0.5F),
                ImVec2(center.x + quarterSize * 0.2F, center.y + halfSize * 0.5F),
                ImVec2(center.x + quarterSize * 0.2F, center.y - quarterSize * 0.3F + bounce),
                ImVec2(center.x + halfSize * 0.5F, center.y - quarterSize * 0.3F + bounce),
            };
            drawList->AddConvexPolyFilled(arrowHead, 7, primary);
            drawList->AddPolyline(arrowHead, 7, secondary, true, 1.5F);
            
            if (isActive)
            {
                // Trail lines
                drawList->AddLine(ImVec2(center.x - quarterSize * 0.6F, center.y + quarterSize), 
                                  ImVec2(center.x - quarterSize * 0.3F, tip.y + quarterSize), IM_COL32(100, 200, 130, 150), 2.0F);
                drawList->AddLine(ImVec2(center.x + quarterSize * 0.6F, center.y + quarterSize), 
                                  ImVec2(center.x + quarterSize * 0.3F, tip.y + quarterSize), IM_COL32(100, 200, 130, 150), 2.0F);
            }
            break;
        }
        case PerkPattern::Fist:
        {
            // Fist/grip
            if (isActive)
            {
                const float shake = std::sin(animPhase * 20.0F) * 2.0F;
                drawList->AddCircleFilled(ImVec2(center.x + shake, center.y), quarterSize * 1.1F, secondary, 16);
            }
            
            // Fingers (4 rounded rectangles)
            for (int i = 0; i < 4; ++i)
            {
                const float angle = -0.3F + static_cast<float>(i) * 0.2F;
                const float fingerX = center.x + std::sin(angle) * quarterSize * 0.6F;
                const float fingerY = center.y - quarterSize * 0.3F - static_cast<float>(i % 2) * quarterSize * 0.2F;
                drawList->AddRectFilled(ImVec2(fingerX - quarterSize * 0.2F, fingerY - quarterSize * 0.4F),
                                        ImVec2(fingerX + quarterSize * 0.2F, fingerY + quarterSize * 0.4F), primary, 3.0F);
            }
            // Palm
            drawList->AddRectFilled(ImVec2(center.x - quarterSize * 0.5F, center.y - quarterSize * 0.1F),
                                    ImVec2(center.x + quarterSize * 0.5F, center.y + halfSize * 0.5F), secondary, 4.0F);
            break;
        }
        case PerkPattern::Eye:
        {
            // All-seeing eye
            const float blink = isActive ? 0.8F + 0.2F * std::sin(animPhase * 3.0F) : 0.85F;
            
            // Eye shape (scaled circle vertically)
            const float eyeRadiusX = halfSize * 0.55F;
            const float eyeRadiusY = quarterSize * blink;
            drawList->PathClear();
            for (int i = 0; i < 24; ++i)
            {
                const float angle = static_cast<float>(i) / 24.0F * 2.0F * 3.14159265F;
                drawList->PathLineTo(ImVec2(center.x + std::cos(angle) * eyeRadiusX, center.y + std::sin(angle) * eyeRadiusY));
            }
            drawList->PathFillConvex(primary);
            // Pupil
            drawList->AddCircleFilled(center, quarterSize * 0.35F, IM_COL32(20, 20, 30, 255), 16);
            // Highlight
            drawList->AddCircleFilled(ImVec2(center.x - quarterSize * 0.15F, center.y - quarterSize * 0.15F), quarterSize * 0.12F, accent, 8);
            
            if (isActive)
            {
                // Rays around eye
                for (int i = 0; i < 8; ++i)
                {
                    const float angle = static_cast<float>(i) * 3.14159F / 4.0F + animPhase * 0.5F;
                    const float innerR = halfSize * 0.7F;
                    const float outerR = halfSize * 0.9F;
                    drawList->AddLine(
                        ImVec2(center.x + std::cos(angle) * innerR, center.y + std::sin(angle) * innerR),
                        ImVec2(center.x + std::cos(angle) * outerR, center.y + std::sin(angle) * outerR),
                        IM_COL32(200, 140, 250, 150), 2.0F
                    );
                }
            }
            break;
        }
        case PerkPattern::Star:
        {
            // 5-pointed star
            const float spin = isActive ? animPhase * 2.0F : 0.0F;
            const float scale = isActive ? 1.0F + 0.1F * std::sin(animPhase * 6.0F) : 1.0F;
            
            ImVec2 starPts[10];
            for (int i = 0; i < 10; ++i)
            {
                const float angle = spin + static_cast<float>(i) * 3.14159F * 2.0F / 10.0F - 3.14159F * 0.5F;
                const float r = (i % 2 == 0) ? halfSize * 0.7F * scale : quarterSize * 0.4F * scale;
                starPts[i] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
            }
            drawList->AddConvexPolyFilled(starPts, 10, primary);
            drawList->AddPolyline(starPts, 10, secondary, true, 1.5F);
            
            if (isActive)
            {
                // Sparkles
                for (int i = 0; i < 4; ++i)
                {
                    const float sparkleAngle = spin * 0.5F + static_cast<float>(i) * 3.14159F * 0.5F;
                    const float sparkleDist = halfSize * 0.9F;
                    const ImVec2 sp(center.x + std::cos(sparkleAngle) * sparkleDist, 
                                    center.y + std::sin(sparkleAngle) * sparkleDist);
                    drawList->AddCircleFilled(sp, 3.0F, accent, 6);
                }
            }
            break;
        }
        case PerkPattern::Hammer:
        {
            // Hammer/breaking icon
            // Handle
            drawList->AddRectFilled(ImVec2(center.x - quarterSize * 0.15F, center.y - quarterSize * 0.5F),
                                    ImVec2(center.x + quarterSize * 0.15F, center.y + halfSize * 0.6F), secondary, 2.0F);
            // Head
            drawList->AddRectFilled(ImVec2(center.x - halfSize * 0.5F, center.y - halfSize * 0.65F),
                                    ImVec2(center.x + halfSize * 0.5F, center.y - quarterSize * 0.4F), primary, 3.0F);
            
            if (isActive)
            {
                // Impact lines
                const float impactPulse = std::sin(animPhase * 15.0F);
                for (int i = 0; i < 5; ++i)
                {
                    const float angle = -0.6F + static_cast<float>(i) * 0.3F;
                    const float len = quarterSize * (0.8F + 0.3F * impactPulse);
                    drawList->AddLine(
                        ImVec2(center.x, center.y - halfSize * 0.65F),
                        ImVec2(center.x + std::sin(angle) * len, center.y - halfSize * 0.65F - std::cos(angle) * len),
                        IM_COL32(255, 200, 100, 200), 2.0F
                    );
                }
            }
            break;
        }
        default:
        {
            // Default hexagon with inner design
            ImVec2 hexPts[6];
            for (int i = 0; i < 6; ++i)
            {
                const float angle = static_cast<float>(i) * 3.14159F / 3.0F - 3.14159F / 6.0F;
                hexPts[i] = ImVec2(center.x + std::cos(angle) * halfSize * 0.65F, 
                                   center.y + std::sin(angle) * halfSize * 0.65F);
            }
            drawList->AddConvexPolyFilled(hexPts, 6, primary);
            drawList->AddPolyline(hexPts, 6, secondary, true, 1.5F);
            // Inner circle
            drawList->AddCircle(center, quarterSize * 0.5F, accent, 12, 1.5F);
            break;
        }
    }
}

void RenderPerkSlotHud(
    const std::array<game::gameplay::HudState::ActivePerkDebug, 4>& perks,
    ImVec2 position,
    bool alignRight,
    bool isKiller
)
{
    constexpr float kSlotSize = 60.0F;        // Size of rhombus (diagonal)
    constexpr float kSlotSpacing = 14.0F;
    constexpr float kPadding = 14.0F;
    constexpr float kIconSize = 28.0F;
    constexpr float kRingRadius = 26.0F;
    constexpr float kRingThickness = 3.5F;
    constexpr float kBottomMargin = 22.0F;

    const float panelWidth = 4.0F * kSlotSize + 3.0F * kSlotSpacing + 2.0F * kPadding;
    const float panelHeight = kSlotSize * 1.3F + 2.0F * kPadding + kBottomMargin;

    // Animation time (simple global time)
    static float globalTime = 0.0F;
    globalTime += ImGui::GetIO().DeltaTime;

    // Adjust position for right alignment
    ImVec2 panelPos = position;
    if (alignRight)
    {
        panelPos.x -= panelWidth;
    }

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PerkHudColors::Background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPadding, kPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("PerksHUD_Procedural", nullptr, flags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        
        // Center Y in the available slot area
        const float slotAreaY = winPos.y + kPadding + kSlotSize * 0.55F;

        for (std::size_t i = 0; i < 4; ++i)
        {
            const auto& perk = perks[i];
            
            // Position along horizontal axis
            const float slotCenterX = winPos.x + kPadding + kSlotSize * 0.5F + static_cast<float>(i) * (kSlotSize + kSlotSpacing);
            const ImVec2 slotCenter(slotCenterX, slotAreaY);

            // Determine perk state
            const bool hasPerk = !perk.id.empty();
            const bool isOnCooldown = hasPerk && perk.cooldownRemainingSeconds > 0.01F;
            const bool isActive = hasPerk && perk.isActive;
            const float halfSize = kSlotSize * 0.5F;

            // Build rhombus (diamond) points
            ImVec2 rhombusPts[4] = {
                ImVec2(slotCenter.x, slotCenter.y - halfSize),
                ImVec2(slotCenter.x + halfSize, slotCenter.y),
                ImVec2(slotCenter.x, slotCenter.y + halfSize),
                ImVec2(slotCenter.x - halfSize, slotCenter.y)
            };

            // Slot background color based on state
            ImU32 slotBgColor;
            if (!hasPerk)
            {
                slotBgColor = IM_COL32(15, 15, 18, 220);
            }
            else if (isActive)
            {
                // Pulsing orange for active
                const float pulse = 0.5F + 0.5F * std::sin(globalTime * 4.0F + static_cast<float>(i));
                slotBgColor = IM_COL32(70 + static_cast<int>(30 * pulse), 45, 20, 250);
            }
            else if (isOnCooldown)
            {
                slotBgColor = IM_COL32(20, 20, 25, 200);
            }
            else
            {
                slotBgColor = IM_COL32(30, 35, 45, 240);
            }

            // Draw rhombus background
            drawList->AddConvexPolyFilled(rhombusPts, 4, slotBgColor);

            // Draw rhombus border with glow for active perks
            ImU32 borderColor;
            if (isActive)
            {
                const float glowPulse = 0.5F + 0.5F * std::sin(globalTime * 5.0F);
                borderColor = IM_COL32(200 + static_cast<int>(55 * glowPulse), 150 + static_cast<int>(50 * glowPulse), 50, 255);
                
                // Outer glow
                const ImVec2 glowPts[4] = {
                    ImVec2(slotCenter.x, slotCenter.y - halfSize - 4),
                    ImVec2(slotCenter.x + halfSize + 4, slotCenter.y),
                    ImVec2(slotCenter.x, slotCenter.y + halfSize + 4),
                    ImVec2(slotCenter.x - halfSize - 4, slotCenter.y)
                };
                drawList->AddPolyline(glowPts, 4, IM_COL32(255, 180, 60, static_cast<int>(80 + 60 * glowPulse)), true, 4.0F);
            }
            else
            {
                borderColor = hasPerk ?
                    (isOnCooldown ? IM_COL32(35, 35, 40, 200) : IM_COL32(60, 70, 85, 255)) :
                    IM_COL32(30, 30, 35, 160);
            }
            drawList->AddPolyline(rhombusPts, 4, borderColor, true, 2.0F);

            if (hasPerk)
            {
                // Get perk colors and pattern
                ImU32 primary, secondary, accent;
                GetPerkColors(perk.id, primary, secondary, accent);
                const PerkPattern pattern = GetPerkPattern(perk.id);

                // Draw procedural icon pattern
                const float animPhase = globalTime + static_cast<float>(i) * 0.4F;
                DrawPerkPattern(drawList, slotCenter, kIconSize * 1.8F, pattern, primary, secondary, accent, animPhase, isActive);

                // Cooldown progress arc
                if (perk.maxCooldownSeconds > 0.01F && !isActive)
                {
                    drawList->AddCircle(slotCenter, kRingRadius, PerkHudColors::CooldownBg, 32, kRingThickness + 1.0F);
                    
                    if (isOnCooldown)
                    {
                        const float cooldownProgress = perk.cooldownRemainingSeconds / perk.maxCooldownSeconds;
                        constexpr float kPi = 3.1415926535F;
                        constexpr float kStartAngle = -kPi * 0.5F;
                        const float endAngle = kStartAngle + cooldownProgress * 2.0F * kPi;
                        drawList->PathArcTo(slotCenter, kRingRadius, kStartAngle, endAngle, 24);
                        drawList->PathStroke(PerkHudColors::CooldownRing, false, kRingThickness);
                    }
                }

                // Tier indicator (dots below the rhombus)
                const int tier = std::clamp(perk.tier, 1, 3);
                const ImU32 tierColor = (tier == 3) ? IM_COL32(240, 170, 50, 255) :
                                        (tier == 2) ? IM_COL32(80, 160, 240, 255) : IM_COL32(120, 120, 130, 255);
                const float tierY = slotCenter.y + halfSize + 10.0F;
                const float tierStartX = slotCenter.x - static_cast<float>(tier - 1) * 6.0F;
                for (int t = 0; t < tier; ++t)
                {
                    drawList->AddCircleFilled(ImVec2(tierStartX + static_cast<float>(t) * 12.0F, tierY), 3.0F, tierColor);
                }

                // Active status text
                if (isActive)
                {
                    const std::string activeText = "ACTIVE";
                    const ImVec2 textSize = ImGui::CalcTextSize(activeText.c_str());
                    const float textX = slotCenter.x - textSize.x * 0.5F;
                    const float textY = slotCenter.y + halfSize * 0.3F;
                    
                    // Background for text
                    drawList->AddRectFilled(ImVec2(textX - 4, textY - 2), ImVec2(textX + textSize.x + 4, textY + textSize.y + 2), 
                                           IM_COL32(0, 0, 0, 150), 3.0F);
                    drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 220, 100, 255), activeText.c_str());
                }
                else if (isOnCooldown)
                {
                    // Show cooldown time
                    const std::string cdText = std::to_string(static_cast<int>(perk.cooldownRemainingSeconds)) + "s";
                    const ImVec2 textSize = ImGui::CalcTextSize(cdText.c_str());
                    const float textX = slotCenter.x - textSize.x * 0.5F;
                    const float textY = slotCenter.y - textSize.y * 0.5F;
                    drawList->AddText(ImVec2(textX, textY), IM_COL32(180, 180, 190, 200), cdText.c_str());
                }
            }
            else
            {
                // Empty slot - draw placeholder
                const ImU32 emptyColor = IM_COL32(40, 40, 50, 180);
                drawList->AddLine(ImVec2(slotCenter.x - 12, slotCenter.y), ImVec2(slotCenter.x + 12, slotCenter.y), emptyColor, 2.5F);
                drawList->AddLine(ImVec2(slotCenter.x, slotCenter.y - 12), ImVec2(slotCenter.x, slotCenter.y + 12), emptyColor, 2.5F);
                
                // Empty text
                const std::string emptyText = "empty";
                const ImVec2 textSize = ImGui::CalcTextSize(emptyText.c_str());
                drawList->AddText(ImVec2(slotCenter.x - textSize.x * 0.5F, slotCenter.y + halfSize * 0.4F), 
                                  IM_COL32(60, 60, 70, 150), emptyText.c_str());
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

std::vector<std::string> Tokenize(const std::string& text)
{
    std::istringstream stream(text);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token)
    {
        tokens.push_back(token);
    }
    return tokens;
}

bool ParseBoolToken(const std::string& token, bool& outValue)
{
    if (token == "on" || token == "true" || token == "1")
    {
        outValue = true;
        return true;
    }
    if (token == "off" || token == "false" || token == "0")
    {
        outValue = false;
        return true;
    }
    return false;
}

float ParseFloatOr(float fallback, const std::string& token)
{
    try
    {
        return std::stof(token);
    }
    catch (...)
    {
        return fallback;
    }
}

int ParseIntOr(int fallback, const std::string& token)
{
    try
    {
        return std::stoi(token);
    }
    catch (...)
    {
        return fallback;
    }
}

std::string CommandCategoryForUsage(const std::string& usage)
{
    const std::vector<std::string> tokens = Tokenize(usage);
    if (tokens.empty())
    {
        return "General";
    }

    const std::string& command = tokens.front();
    if (command == "host" || command == "join" || command == "disconnect" || command == "net_status" ||
        command == "net_dump" || command == "lan_scan" || command == "lan_status" || command == "lan_debug")
    {
        return "Network";
    }
    if (command == "set_vsync" || command == "set_fps" || command == "set_tick" || command == "set_resolution" ||
        command == "toggle_fullscreen" || command == "render_mode" || command == "audio_play" ||
        command == "audio_loop" || command == "audio_stop_all" ||
        command == "perf" || command == "perf_pin" || command == "perf_compact" ||
        command == "benchmark" || command == "benchmark_stop" ||
        command == "perf_test" || command == "perf_report")
    {
        return "System";
    }
    if (command == "toggle_collision" || command == "toggle_debug_draw" || command == "physics_debug" ||
        command == "noclip" || command == "tr_vis" || command == "tr_set" || command == "set_chase" ||
        command == "cam_mode" || command == "control_role" || command == "set_role" ||
        command == "trap_spawn" || command == "trap_clear" || command == "trap_debug" ||
        command == "item_respawn_near" || command == "item_ids" || command == "items" || command == "list_items" ||
        command == "power_ids" || command == "powers" || command == "list_powers" ||
        command == "item_spawn" || command == "spawn_item" || command == "spawn_item_here" ||
        command == "item_dump" || command == "power_dump" || command == "set_survivor" || command == "set_killer" ||
        command == "fx_spawn" || command == "fx_stop_all" || command == "fx_list" ||
        command == "player_dump" || command == "scene_dump")
    {
        return "Debug";
    }
    if (command == "help" || command == "quit")
    {
        return "General";
    }
    return "Gameplay";
}
} // namespace

struct DeveloperConsole::Impl
{
    struct CommandInfo
    {
        std::string usage;
        std::string description;
        std::string category;
    };

    struct LogEntry
    {
        std::string text;
        glm::vec4 color;
        bool isCommand = false;
        int categoryDepth = 0;
    };

    using CommandHandler = std::function<void(const std::vector<std::string>&, const ConsoleContext&)>;

    bool open = false;
    bool firstOpenAnnouncementDone = false;
    bool scrollToBottom = false;
    bool reclaimFocus = false;

    std::array<char, 512> inputBuffer{};

    std::vector<LogEntry> items;
    std::vector<std::string> history;
    int historyPos = -1;

    std::unordered_map<std::string, CommandHandler> commandRegistry;
    std::vector<CommandInfo> commandInfos;

    int completionCycleIndex = 0;
    std::string lastCompletionInput;

    void AddLog(const std::string& text, const glm::vec4& color = ConsoleColors::Default, bool isCommand = false, int categoryDepth = 0)
    {
        items.push_back(LogEntry{text, color, isCommand, categoryDepth});
        scrollToBottom = true;
    }

    void LogCommand(const std::string& text) { AddLog(text, ConsoleColors::Command, true, 0); }
    void LogSuccess(const std::string& text) { AddLog("✓ " + text, ConsoleColors::Success, false, 0); }
    void LogError(const std::string& text) { AddLog("✗ " + text, ConsoleColors::Error, false, 0); }
    void LogWarning(const std::string& text) { AddLog("⚠ " + text, ConsoleColors::Warning, false, 0); }
    void LogInfo(const std::string& text) { AddLog(text, ConsoleColors::Info, false, 0); }
    void LogCategory(const std::string& text) { AddLog(text, ConsoleColors::Category, false, 0); }
    void LogValue(const std::string& label, const std::string& value)
    {
        AddLog("  " + label + ": " + value, ConsoleColors::Info, false, 0);
    }

    void PrintHelp()
    {
        AddLog("Available commands by category:", ConsoleColors::Category);
        std::map<std::string, std::vector<CommandInfo>> grouped;
        for (const CommandInfo& info : commandInfos)
        {
            grouped[info.category].push_back(info);
        }

        for (auto& [category, commands] : grouped)
        {
            std::sort(commands.begin(), commands.end(), [](const CommandInfo& a, const CommandInfo& b) {
                return a.usage < b.usage;
            });
            AddLog("▸ " + category, ConsoleColors::Category);
            for (const CommandInfo& info : commands)
            {
                AddLog("  • " + info.usage + " — " + info.description, ConsoleColors::Info, false, 1);
            }
        }
    }

    void RegisterCommand(const std::string& usage, const std::string& description, CommandHandler handler)
    {
        const std::vector<std::string> tokens = Tokenize(usage);
        if (tokens.empty())
        {
            return;
        }

        commandInfos.push_back(CommandInfo{usage, description, CommandCategoryForUsage(usage)});
        commandRegistry[tokens.front()] = std::move(handler);
    }

    std::vector<std::string> GetParamOptions(const std::string& commandName, int paramIndex) const
    {
        std::vector<std::string> options;

        for (const CommandInfo& info : commandInfos)
        {
            if (info.usage.starts_with(commandName + " "))
            {
                std::vector<std::string> tokens = Tokenize(info.usage);
                if (tokens.size() > static_cast<std::size_t>(paramIndex + 1))
                {
                    const std::string& paramToken = tokens[static_cast<std::size_t>(paramIndex + 1)];
                    if (paramToken.find('|') != std::string::npos)
                    {
                        size_t start = 0;
                        size_t end = paramToken.find('|');
                        while (end != std::string::npos)
                        {
                            options.push_back(paramToken.substr(start, end - start));
                            start = end + 1;
                            end = paramToken.find('|', start);
                        }
                        options.push_back(paramToken.substr(start));
                    }
                }
                break;
            }
        }
        return options;
    }

    std::vector<CommandInfo> BuildHints(const std::string& inputText) const
    {
        std::vector<CommandInfo> hints;
        const std::vector<std::string> inputTokens = Tokenize(inputText);
        const std::string prefix = inputTokens.empty() ? std::string{} : inputTokens.front();

        for (const CommandInfo& info : commandInfos)
        {
            if (prefix.empty() || info.usage.rfind(prefix, 0) == 0)
            {
                hints.push_back(info);
            }
        }
        std::sort(hints.begin(), hints.end(), [](const CommandInfo& a, const CommandInfo& b) {
            if (a.category == b.category)
            {
                return a.usage < b.usage;
            }
            return a.category < b.category;
        });
        return hints;
    }

    void RegisterDefaultCommands()
    {
RegisterCommand("clear", "Clear console output", [this](const std::vector<std::string>&, const ConsoleContext&) {
            items.clear();
        });

        RegisterCommand("help", "List all commands", [this](const std::vector<std::string>&, const ConsoleContext&) {
            PrintHelp();
        });

        RegisterCommand("fx_spawn <assetId>", "Spawn an FX asset at camera forward", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: fx_spawn <assetId>");
                return;
            }

            context.gameplay->SpawnFxDebug(tokens[1]);
            LogSuccess("FX spawned: " + tokens[1]);
        });

        RegisterCommand("fx_stop_all", "Stop all active FX instances", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            context.gameplay->StopAllFx();
            LogSuccess("All FX stopped");
        });

        RegisterCommand("fx_list", "List available FX assets", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            const std::vector<std::string> assets = context.gameplay->ListFxAssets();
            if (assets.empty())
            {
                LogWarning("No FX assets found");
                return;
            }
            AddLog("FX assets:", ConsoleColors::Category);
            for (const std::string& assetId : assets)
            {
                AddLog("  • " + assetId, ConsoleColors::Info);
            }
        });

        RegisterCommand("audio_play <clip> [bus]", "Play one-shot audio clip (bus: music|sfx|ui|ambience)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() < 2 || context.audioPlay == nullptr)
            {
                LogError("Usage: audio_play <clip> [bus]");
                return;
            }
            const std::string bus = tokens.size() >= 3 ? tokens[2] : "sfx";
            context.audioPlay(tokens[1], bus, false);
            LogSuccess("Audio one-shot started: " + tokens[1] + " (" + bus + ")");
        });

        RegisterCommand("audio_loop <clip> [bus]", "Play looping audio clip (bus: music|sfx|ui|ambience)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() < 2 || context.audioPlay == nullptr)
            {
                LogError("Usage: audio_loop <clip> [bus]");
                return;
            }
            const std::string bus = tokens.size() >= 3 ? tokens[2] : "music";
            context.audioPlay(tokens[1], bus, true);
            LogSuccess("Audio loop started: " + tokens[1] + " (" + bus + ")");
        });

        RegisterCommand("audio_stop_all", "Stop all active audio loops/sounds", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.audioStopAll)
            {
                context.audioStopAll();
                LogSuccess("All audio stopped");
            }
        });

        // ─── Profiler commands ───
        RegisterCommand("perf", "Toggle performance profiler overlay", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.profilerToggle) {
                context.profilerToggle();
                LogSuccess("Profiler toggled");
            }
        });

        RegisterCommand("perf_pin on|off", "Pin/unpin profiler to game window", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.profilerSetPinned && tokens.size() >= 2) {
                const bool pinned = tokens[1] == "on" || tokens[1] == "1";
                context.profilerSetPinned(pinned);
                LogSuccess(pinned ? "Profiler pinned" : "Profiler unpinned");
            }
        });

        RegisterCommand("perf_compact on|off", "Toggle compact profiler bar", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.profilerSetCompact && tokens.size() >= 2) {
                const bool compact = tokens[1] == "on" || tokens[1] == "1";
                context.profilerSetCompact(compact);
                LogSuccess(compact ? "Compact mode ON" : "Compact mode OFF");
            }
        });

        RegisterCommand("benchmark [frames]", "Run automated performance benchmark (default 600 frames)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.profilerBenchmark) {
                int frames = 600;
                if (tokens.size() >= 2) {
                    try { frames = std::stoi(tokens[1]); } catch (...) {}
                }
                context.profilerBenchmark(frames);
                LogSuccess("Benchmark started (" + std::to_string(frames) + " frames)");
            }
        });

        RegisterCommand("benchmark_stop", "Stop running benchmark", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.profilerBenchmarkStop) {
                context.profilerBenchmarkStop();
                LogSuccess("Benchmark stopped");
            }
        });

        RegisterCommand("perf_test [map] [frames]", "Run automated perf test on a map (default: main, 600 frames)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (!context.perfTest)
            {
                LogError("perf_test not available");
                return;
            }
            std::string mapName = "main";
            int frames = 600;
            if (tokens.size() > 1) mapName = tokens[1];
            if (tokens.size() > 2)
            {
                try { frames = std::stoi(tokens[2]); }
                catch (...) { frames = 600; }
            }
            if (frames < 60) frames = 60;
            if (frames > 10000) frames = 10000;
            LogInfo("Starting perf test: map=" + mapName + " frames=" + std::to_string(frames));
            context.perfTest(mapName, frames);
        });

        RegisterCommand("perf_report", "Print last benchmark results", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (!context.perfReport)
            {
                LogError("perf_report not available");
                return;
            }
            const std::string report = context.perfReport();
            if (report.empty())
            {
                LogInfo("No benchmark results available. Run 'benchmark' or 'perf_test' first.");
            }
            else
            {
                LogInfo(report);
            }
        });

        RegisterCommand("spawn survivor|killer|pallet|window", "Spawn gameplay entities", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() < 2)
            {
                LogError("Usage: spawn survivor|killer|pallet|window");
                return;
            }

            if (tokens[1] == "survivor")
            {
                context.gameplay->SpawnSurvivor();
                LogSuccess("Spawned survivor");
            }
            else if (tokens[1] == "killer")
            {
                context.gameplay->SpawnKiller();
                LogSuccess("Spawned killer");
            }
            else if (tokens[1] == "pallet")
            {
                context.gameplay->SpawnPallet();
                LogSuccess("Spawned pallet");
            }
            else if (tokens[1] == "window")
            {
                context.gameplay->SpawnWindow();
                LogSuccess("Spawned window");
            }
            else
            {
                LogError("Unknown spawn target");
            }
        });

        RegisterCommand("spawn_survivor_here", "Spawn/respawn survivor at camera projected ground", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.spawnRoleHere)
            {
                context.spawnRoleHere("survivor");
                LogSuccess("Survivor spawned at camera position");
            }
        });

        RegisterCommand("spawn_killer_here", "Spawn/respawn killer at camera projected ground", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.spawnRoleHere)
            {
                context.spawnRoleHere("killer");
                LogSuccess("Killer spawned at camera position");
            }
        });

        RegisterCommand("spawn_survivor_at <spawnId>", "Spawn/respawn survivor at spawn point ID", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.spawnRoleAt == nullptr)
            {
                LogError("Usage: spawn_survivor_at <spawnId>");
                return;
            }
            context.spawnRoleAt("survivor", ParseIntOr(-1, tokens[1]));
            LogSuccess("Survivor spawned at point " + tokens[1]);
        });

        RegisterCommand("spawn_killer_at <spawnId>", "Spawn/respawn killer at spawn point ID", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.spawnRoleAt == nullptr)
            {
                LogError("Usage: spawn_killer_at <spawnId>");
                return;
            }
            context.spawnRoleAt("killer", ParseIntOr(-1, tokens[1]));
            LogSuccess("Killer spawned at point " + tokens[1]);
        });

        RegisterCommand("list_spawns", "List spawn points with IDs", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.listSpawns)
            {
                AddLog(context.listSpawns());
            }
        });

        RegisterCommand("teleport survivor|killer x y z", "Teleport survivor or killer", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 5)
            {
                LogError("Usage: teleport survivor|killer x y z");
                return;
            }

            const glm::vec3 position{
                ParseFloatOr(0.0F, tokens[2]),
                ParseFloatOr(1.0F, tokens[3]),
                ParseFloatOr(0.0F, tokens[4]),
            };

            if (tokens[1] == "survivor")
            {
                context.gameplay->TeleportSurvivor(position);
                LogSuccess("Teleported survivor to (" + tokens[2] + ", " + tokens[3] + ", " + tokens[4] + ")");
            }
            else if (tokens[1] == "killer")
            {
                context.gameplay->TeleportKiller(position);
                LogSuccess("Teleported killer to (" + tokens[2] + ", " + tokens[3] + ", " + tokens[4] + ")");
            }
            else
            {
                LogError("Unknown teleport target");
            }
        });

        RegisterCommand("give_speed survivor 6.0", "Set survivor sprint speed", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 3 || tokens[1] != "survivor")
            {
                LogError("Usage: give_speed survivor 6.0");
                return;
            }

            context.gameplay->SetSurvivorSprintSpeed(ParseFloatOr(6.0F, tokens[2]));
            LogSuccess("Survivor sprint speed set to " + tokens[2]);
        });

        RegisterCommand("set_speed survivor|killer <percent>", "Set role movement speed percent (100 = default)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 3)
            {
                LogError("Usage: set_speed survivor|killer <percent>");
                return;
            }
            if (tokens[1] != "survivor" && tokens[1] != "killer")
            {
                LogError("Role must be survivor or killer");
                return;
            }

            float value = ParseFloatOr(100.0F, tokens[2]);
            if (value <= 0.0F)
            {
                LogError("Percent must be > 0");
                return;
            }

            if (value > 10.0F)
            {
                value *= 0.01F;
            }
            context.gameplay->SetRoleSpeedPercent(tokens[1], value);
            LogSuccess(tokens[1] + " speed multiplier set to " + std::to_string(value));
        });

        RegisterCommand("set_size survivor|killer <radius> <height>", "Set role capsule size", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 4)
            {
                LogError("Usage: set_size survivor|killer <radius> <height>");
                return;
            }
            if (tokens[1] != "survivor" && tokens[1] != "killer")
            {
                LogError("Role must be survivor or killer");
                return;
            }

            const float radius = ParseFloatOr(0.35F, tokens[2]);
            const float height = ParseFloatOr(1.8F, tokens[3]);
            context.gameplay->SetRoleCapsuleSize(tokens[1], radius, height);
            LogSuccess(tokens[1] + " capsule size: r=" + tokens[2] + " h=" + tokens[3]);
        });

        RegisterCommand("heal survivor", "Heal survivor (Injured -> Healthy)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2 || tokens[1] != "survivor")
            {
                LogError("Usage: heal survivor");
                return;
            }

            context.gameplay->HealSurvivor();
            LogSuccess("Survivor healed");
        });

        RegisterCommand("survivor_state healthy|injured|downed|trapped|carried|hooked|dead", "Force survivor FSM state (debug)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: survivor_state healthy|injured|downed|trapped|carried|hooked|dead");
                return;
            }

            context.gameplay->SetSurvivorStateDebug(tokens[1]);
            LogSuccess("Survivor state set to: " + tokens[1]);
        });

        RegisterCommand("set_generators_done 0..5", "Set generator completion count", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: set_generators_done <count>");
                return;
            }

            const int count = ParseIntOr(0, tokens[1]);
            context.gameplay->SetGeneratorsCompleted(count);
            LogSuccess("Generators completed: " + tokens[1]);
        });

        RegisterCommand("hook_survivor", "Hook carried survivor on nearest hook", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }

            context.gameplay->HookCarriedSurvivorDebug();
            LogSuccess("Survivor hook requested");
        });

        RegisterCommand("skillcheck start", "Start skillcheck widget (debug)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2 || tokens[1] != "start")
            {
                LogError("Usage: skillcheck start");
                return;
            }

            context.gameplay->StartSkillCheckDebug();
            LogSuccess("Skillcheck started");
        });

        RegisterCommand("toggle_collision on|off", "Enable/disable collision", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: toggle_collision on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.gameplay->ToggleCollision(enabled);
            LogSuccess(std::string("Collision ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("toggle_debug_draw on|off", "Enable/disable collider and trigger debug draw", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: toggle_debug_draw on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.gameplay->ToggleDebugDraw(enabled);
            LogSuccess(std::string("Debug draw ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("physics_debug on|off", "Toggle physics debug readout", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: physics_debug on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.gameplay->TogglePhysicsDebug(enabled);
            if (context.setPhysicsDebug)
            {
                context.setPhysicsDebug(enabled);
            }
            LogSuccess(std::string("Physics debug ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("noclip on|off", "Toggle noclip for players", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: noclip on|off");
                return;
            }

            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.gameplay->SetNoClip(enabled);
            if (context.setNoClip)
            {
                context.setNoClip(enabled);
            }
            LogSuccess(std::string("Noclip ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("load map test|main|main_map|collision_test", "Load gameplay scene", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 3 || tokens[1] != "map")
            {
                LogError("Usage: load map test|main|main_map|collision_test");
                return;
            }

            std::string mapName = tokens[2];
            if (mapName == "main_map")
            {
                mapName = "main";
            }
            context.gameplay->LoadMap(mapName);
            LogSuccess("Map loaded: " + mapName);
        });

        RegisterCommand("host [port]", "Host listen server (default 7777)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.hostSession == nullptr || tokens.size() > 2)
            {
                LogError("Usage: host [port]");
                return;
            }

            const int port = std::clamp(ParseIntOr(7777, tokens.size() == 2 ? tokens[1] : "7777"), 1, 65535);
            context.hostSession(port);
            LogSuccess("Hosting on port " + std::to_string(port));
        });

        RegisterCommand("join <ip> <port>", "Join listen server", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.joinSession == nullptr || tokens.size() != 3)
            {
                LogError("Usage: join <ip> <port>");
                return;
            }

            const int port = std::clamp(ParseIntOr(7777, tokens[2]), 1, 65535);
            context.joinSession(tokens[1], port);
            LogSuccess("Connecting to " + tokens[1] + ":" + tokens[2]);
        });

        RegisterCommand("disconnect", "Disconnect and return to menu", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.disconnectSession)
            {
                context.disconnectSession();
                LogSuccess("Disconnected");
            }
        });

        RegisterCommand("net_status", "Print network state and diagnostics", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.netStatus)
            {
                AddLog(context.netStatus());
            }
        });

        RegisterCommand("net_dump", "Print network config/tick/interpolation", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.netDump)
            {
                AddLog(context.netDump());
            }
        });

        RegisterCommand("lan_scan", "Force LAN discovery scan", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.lanScan)
            {
                context.lanScan();
                LogSuccess("LAN scan started");
            }
        });

        RegisterCommand("lan_status", "Print LAN discovery status", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.lanStatus)
            {
                AddLog(context.lanStatus());
            }
        });

        RegisterCommand("lan_debug on|off", "Toggle LAN discovery debug", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.lanDebug == nullptr)
            {
                LogError("Usage: lan_debug on|off");
                return;
            }

            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.lanDebug(enabled);
            LogSuccess(std::string("LAN debug ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("tr_vis on|off", "Toggle terror radius visualization", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                LogError("Usage: tr_vis on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            if (context.gameplay != nullptr)
            {
                context.gameplay->ToggleTerrorRadiusVisualization(enabled);
            }
            if (context.setTerrorRadiusVisible)
            {
                context.setTerrorRadiusVisible(enabled);
            }
            LogSuccess(std::string("Terror radius visual ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("tr_set <meters>", "Set terror radius meters", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                LogError("Usage: tr_set <meters>");
                return;
            }

            const float meters = std::max(1.0F, ParseFloatOr(24.0F, tokens[1]));
            if (context.gameplay != nullptr)
            {
                context.gameplay->SetTerrorRadius(meters);
            }
            if (context.setTerrorRadiusMeters)
            {
                context.setTerrorRadiusMeters(meters);
            }
            LogSuccess("Terror radius set to " + std::to_string(meters) + "m");
        });

        RegisterCommand("tr_debug on|off", "Toggle terror radius audio debug mode", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.setTerrorAudioDebug == nullptr)
            {
                LogError("Usage: tr_debug on|off");
                return;
            }
            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }
            context.setTerrorAudioDebug(enabled);
            LogSuccess(std::string("Terror radius audio debug ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("tr_dump", "Print terror radius state, band, per-layer volumes", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.terrorRadiusDump == nullptr)
            {
                LogError("Terror radius dump not available");
                return;
            }
            AddLog(context.terrorRadiusDump());
        });

        // Alias for tr_set
        RegisterCommand("tr_radius <m>", "Set terror radius (alias for tr_set)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                LogError("Usage: tr_radius <meters>");
                return;
            }

            const float meters = std::max(1.0F, ParseFloatOr(32.0F, tokens[1]));
            if (context.gameplay != nullptr)
            {
                context.gameplay->SetTerrorRadius(meters);
            }
            if (context.setTerrorRadiusMeters)
            {
                context.setTerrorRadiusMeters(meters);
            }
            LogSuccess("Terror radius set to " + std::to_string(meters) + "m");
        });

        RegisterCommand("regen_loops [seed]", "Regenerate loop layout on main map (optional deterministic seed)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }

            if (tokens.size() == 2)
            {
                const int parsedSeed = ParseIntOr(1337, tokens[1]);
                context.gameplay->RegenerateLoops(static_cast<unsigned int>(std::max(1, parsedSeed)));
                LogSuccess("Regenerated loops with seed " + tokens[1]);
            }
            else
            {
                context.gameplay->RegenerateLoops();
                LogSuccess("Regenerated loops with new seed");
            }
        });

        RegisterCommand("dbd_spawns on|off", "Enable/disable DBD-inspired spawn system", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: dbd_spawns on|off");
                return;
            }

            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.gameplay->SetDbdSpawnsEnabled(enabled);
            LogSuccess(std::string("DBD spawns ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("perks <list|equip|clear|reset>", "Manage perks (list/equip/clear/reset)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }

            if (tokens.size() < 2)
            {
                LogError("Usage: perks <list|equip|clear|reset>");
                AddLog("  perks list");
                AddLog("  perks equip <role> <slot> <id>");
                AddLog("  perks clear <role>");
                AddLog("  perks reset");
                return;
            }

            const std::string& subcommand = tokens[1];
            if (subcommand == "list")
            {
                const auto& perkSystem = context.gameplay->GetPerkSystem();
                std::vector<std::string> survivorPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor);
                std::vector<std::string> killerPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
                std::vector<std::string> bothPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Both);

                AddLog("=== SURVIVOR PERKS ===");
                for (const auto& id : survivorPerks)
                {
                    const auto* perk = perkSystem.GetPerk(id);
                    if (perk)
                    {
                        AddLog(id + " - " + perk->name);
                    }
                }

                AddLog("=== KILLER PERKS ===");
                for (const auto& id : killerPerks)
                {
                    const auto* perk = perkSystem.GetPerk(id);
                    if (perk)
                    {
                        AddLog(id + " - " + perk->name);
                    }
                }

                if (!bothPerks.empty())
                {
                    AddLog("=== BOTH ROLES ===");
                    for (const auto& id : bothPerks)
                    {
                        const auto* perk = perkSystem.GetPerk(id);
                        if (perk)
                        {
                            AddLog(id + " - " + perk->name);
                        }
                    }
                }

                AddLog("Total: " + std::to_string(survivorPerks.size() + killerPerks.size() + bothPerks.size()) + " perks");
                return;
            }

            if (subcommand == "equip")
            {
                if (tokens.size() != 5)
                {
                    LogError("Usage: perks equip <role> <slot> <id>");
                    AddLog("  role: survivor | killer");
                    AddLog("  slot: 0 | 1 | 2");
                    AddLog("  id: perk_id (use 'perks list' to see available)");
                    return;
                }

                const std::string& roleName = tokens[2];
                if (roleName != "survivor" && roleName != "killer")
                {
                    LogError("Role must be survivor or killer");
                    return;
                }

                const int slot = ParseIntOr(-1, tokens[3]);
                if (slot < 0 || slot > 2)
                {
                    LogError("Invalid slot (must be 0, 1, or 2)");
                    return;
                }

                const std::string& perkId = tokens[4];
                const auto& perkSystem = context.gameplay->GetPerkSystem();
                const auto* perk = perkSystem.GetPerk(perkId);
                if (!perk)
                {
                    LogError("Perk not found: " + perkId + " (use 'perks list' to see available)");
                    return;
                }

                const auto role = (roleName == "survivor") ? game::gameplay::perks::PerkRole::Survivor : game::gameplay::perks::PerkRole::Killer;
                if (perk->role != game::gameplay::perks::PerkRole::Both && perk->role != role)
                {
                    LogError("Perk '" + perk->name + "' is not for " + roleName);
                    return;
                }

                game::gameplay::perks::PerkLoadout loadout;
                if (roleName == "survivor")
                {
                    loadout = perkSystem.GetSurvivorLoadout();
                }
                else
                {
                    loadout = perkSystem.GetKillerLoadout();
                }

                loadout.SetPerk(slot, perkId);

                if (roleName == "survivor")
                {
                    context.gameplay->SetSurvivorPerkLoadout(loadout);
                }
                else
                {
                    context.gameplay->SetKillerPerkLoadout(loadout);
                }

                LogSuccess("Equipped '" + perk->name + "' for " + roleName + " in slot " + std::to_string(slot));
                return;
            }

            if (subcommand == "clear")
            {
                if (tokens.size() != 3)
                {
                    LogError("Usage: perks clear <role>");
                    AddLog("  role: survivor | killer");
                    return;
                }

                const std::string& roleName = tokens[2];
                if (roleName != "survivor" && roleName != "killer")
                {
                    LogError("Role must be survivor or killer");
                    return;
                }

                game::gameplay::perks::PerkLoadout loadout;
                loadout.Clear();

                if (roleName == "survivor")
                {
                    context.gameplay->SetSurvivorPerkLoadout(loadout);
                }
                else
                {
                    context.gameplay->SetKillerPerkLoadout(loadout);
                }

                LogSuccess("Cleared all perks for " + roleName);
                return;
            }

            if (subcommand == "reset")
            {
                context.gameplay->GetPerkSystem().SetDefaultDevLoadout();
                LogSuccess("Reset perks to default dev loadout");
                return;
            }

            LogError("Unknown perks subcommand. Use: list | equip | clear | reset");
        });

        RegisterCommand("chase_force on|off", "Force chase state on/off", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: chase_force on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.gameplay->SetForcedChase(enabled);
            LogSuccess(std::string("Forced chase ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("set_chase on|off", "Alias for chase_force", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: set_chase on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            context.gameplay->SetForcedChase(enabled);
            LogSuccess(std::string("Forced chase ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("chase_dump", "Print chase state debug info", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            const auto hudState = context.gameplay->BuildHudState();
            AddLog("=== Chase State ===");
            AddLog(std::string("Active: ") + (hudState.chaseActive ? "YES" : "NO"));
            AddLog("Distance: " + std::to_string(hudState.chaseDistance) + "m");
            AddLog(std::string("Line of Sight: ") + (hudState.lineOfSight ? "YES" : "NO"));
            AddLog(std::string("In Center FOV: ") + (hudState.inCenterFOV ? "YES" : "NO"));
            AddLog(std::string("Survivor Sprinting: ") + (hudState.survivorSprinting ? "YES" : "NO"));
            AddLog("Time in Chase: " + std::to_string(hudState.timeInChase) + "s");
            AddLog("Time Since LOS: " + std::to_string(hudState.timeSinceLOS) + "s");
            AddLog("Time Since Center FOV: " + std::to_string(hudState.timeSinceCenterFOV) + "s");
        });

        RegisterCommand("bloodlust_reset", "Reset bloodlust to tier 0", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            context.gameplay->ResetBloodlust();
            LogSuccess("Bloodlust reset to tier 0");
        });

        RegisterCommand("bloodlust_set <0|1|2|3>", "Set bloodlust tier directly", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: bloodlust_set <0|1|2|3>");
                return;
            }
            const int tier = ParseIntOr(0, tokens[1]);
            if (tier < 0 || tier > 3)
            {
                LogError("Tier must be between 0 and 3");
                return;
            }
            context.gameplay->SetBloodlustTier(tier);
            LogSuccess("Bloodlust tier set to " + std::to_string(tier));
        });

        RegisterCommand("bloodlust_dump", "Print bloodlust state and speed info", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            const auto hudState = context.gameplay->BuildHudState();
            AddLog("=== Bloodlust State ===");
            AddLog("Tier: " + std::to_string(hudState.bloodlustTier));
            AddLog("Speed Multiplier: " + std::to_string(hudState.bloodlustSpeedMultiplier));
            AddLog("Killer Base Speed: " + std::to_string(hudState.killerBaseSpeed) + " m/s");
            AddLog("Killer Current Speed: " + std::to_string(hudState.killerCurrentSpeed) + " m/s");
        });

        RegisterCommand("scratch_debug on|off", "Toggle scratch marks debug overlay", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: scratch_debug on|off");
                return;
            }
            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }
            context.gameplay->SetScratchDebug(enabled);
            LogSuccess(std::string("Scratch debug ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("scratch_profile <name>", "Load scratch profile (future: from JSON)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            const std::string profile = tokens.size() > 1 ? tokens[1] : "default";
            context.gameplay->SetScratchProfile(profile);
            LogSuccess("Scratch profile set to: " + profile);
        });

        RegisterCommand("blood_debug on|off", "Toggle blood pools debug overlay", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: blood_debug on|off");
                return;
            }
            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }
            context.gameplay->SetBloodDebug(enabled);
            LogSuccess(std::string("Blood debug ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("blood_profile <name>", "Load blood pool profile (future: from JSON)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            const std::string profile = tokens.size() > 1 ? tokens[1] : "default";
            context.gameplay->SetBloodProfile(profile);
            LogSuccess("Blood profile set to: " + profile);
        });

        RegisterCommand("killer_light on|off", "Toggle killer look light", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: killer_light on|off");
                return;
            }
            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }
            context.gameplay->SetKillerLookLightEnabled(enabled);
            LogSuccess(std::string("Killer light ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("killer_light_range <m>", "Set killer light range", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: killer_light_range <meters>");
                return;
            }
            const float range = ParseFloatOr(0.0F, tokens[1]);
            if (range <= 0.0F || range > 100.0F)
            {
                LogError("Range must be between 0 and 100");
                return;
            }
            context.gameplay->SetKillerLookLightRange(range);
            LogSuccess("Killer light range set to " + std::to_string(range) + "m");
        });

        RegisterCommand("killer_light_debug on|off", "Toggle killer light debug overlay", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: killer_light_debug on|off");
                return;
            }
            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }
            context.gameplay->SetKillerLookLightDebug(enabled);
            LogSuccess(std::string("Killer light debug ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("killer_light_intensity <float>", "Set killer light intensity", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: killer_light_intensity <value>");
                return;
            }
            const float intensity = ParseFloatOr(1.1F, tokens[1]);
            if (intensity < 0.0F || intensity > 20.0F)
            {
                LogError("Intensity must be between 0 and 20");
                return;
            }
            context.gameplay->SetKillerLookLightIntensity(intensity);
            LogSuccess("Killer light intensity set to " + std::to_string(intensity));
        });

        RegisterCommand("killer_light_angle <deg>", "Set killer light cone angle", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: killer_light_angle <degrees>");
                return;
            }
            const float angle = ParseFloatOr(16.0F, tokens[1]);
            if (angle < 1.0F || angle > 90.0F)
            {
                LogError("Angle must be between 1 and 90");
                return;
            }
            context.gameplay->SetKillerLookLightAngle(angle);
            LogSuccess("Killer light angle set to " + std::to_string(angle) + " degrees");
        });

        RegisterCommand("killer_light_outer <deg>", "Set killer light outer cone angle", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: killer_light_outer <degrees>");
                return;
            }
            const float angle = ParseFloatOr(28.0F, tokens[1]);
            if (angle < 2.0F || angle > 90.0F)
            {
                LogError("Outer angle must be between 2 and 90");
                return;
            }
            context.gameplay->SetKillerLookLightOuterAngle(angle);
            LogSuccess("Killer light outer angle set to " + std::to_string(angle) + " degrees");
        });

        RegisterCommand("killer_light_pitch <deg>", "Set killer light pitch (downward angle, 0=horizontal, 90=down)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: killer_light_pitch <degrees>");
                return;
            }
            const float pitch = ParseFloatOr(35.0F, tokens[1]);
            if (pitch < 0.0F || pitch > 90.0F)
            {
                LogError("Pitch must be between 0 and 90 degrees");
                return;
            }
            context.gameplay->SetKillerLookLightPitch(pitch);
            LogSuccess("Killer light pitch set to " + std::to_string(pitch) + " degrees");
        });

        RegisterCommand("cam_mode survivor|killer|role", "Force camera mode (3rd/1st/role-based)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: cam_mode survivor|killer|role");
                return;
            }

            context.gameplay->SetCameraModeOverride(tokens[1]);
            if (context.setCameraMode)
            {
                context.setCameraMode(tokens[1]);
            }
            LogSuccess("Camera mode: " + tokens[1]);
        });

        RegisterCommand("control_role survivor|killer", "Switch controlled role", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: control_role survivor|killer");
                return;
            }

            if (context.requestRoleChange)
            {
                context.requestRoleChange(tokens[1]);
            }
            else
            {
                context.gameplay->SetControlledRole(tokens[1]);
                if (context.setControlledRole)
                {
                    context.setControlledRole(tokens[1]);
                }
            }
            LogSuccess("Controlled role: " + tokens[1]);
        });

        RegisterCommand("set_role survivor|killer", "Alias for control_role", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: set_role survivor|killer");
                return;
            }

            if (context.requestRoleChange)
            {
                context.requestRoleChange(tokens[1]);
            }
            else
            {
                context.gameplay->SetControlledRole(tokens[1]);
                if (context.setControlledRole)
                {
                    context.setControlledRole(tokens[1]);
                }
            }
            LogSuccess("Role set: " + tokens[1]);
        });

        RegisterCommand("player_dump", "Print player->pawn ownership mapping", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.playerDump)
            {
                AddLog(context.playerDump());
            }
        });

        RegisterCommand("scene_dump", "Print current scene entities summary", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.sceneDump)
            {
                AddLog(context.sceneDump());
            }
        });

        RegisterCommand("item_set <id|none>", "Set survivor item loadout item id", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: item_set <id|none>");
                return;
            }
            auto hud = context.gameplay->BuildHudState();
            const std::string itemId = tokens[1] == "none" ? "" : tokens[1];
            if (!context.gameplay->SetSurvivorItemLoadout(itemId, hud.survivorItemAddonA == "none" ? "" : hud.survivorItemAddonA, hud.survivorItemAddonB == "none" ? "" : hud.survivorItemAddonB))
            {
                LogError("item_set failed (invalid id/addon mismatch)");
                return;
            }
            LogSuccess("item_set: " + (itemId.empty() ? "none" : itemId));
        });

        RegisterCommand("power_set <id|none>", "Set killer power id", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: power_set <id|none>");
                return;
            }
            auto hud = context.gameplay->BuildHudState();
            const std::string powerId = tokens[1] == "none" ? "" : tokens[1];
            if (!context.gameplay->SetKillerPowerLoadout(powerId, hud.killerPowerAddonA == "none" ? "" : hud.killerPowerAddonA, hud.killerPowerAddonB == "none" ? "" : hud.killerPowerAddonB))
            {
                LogError("power_set failed (invalid id/addon mismatch)");
                return;
            }
            LogSuccess("power_set: " + (powerId.empty() ? "none" : powerId));
        });

        RegisterCommand("item_addon_a <id|none>", "Set survivor item addon A", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: item_addon_a <id|none>");
                return;
            }
            auto hud = context.gameplay->BuildHudState();
            const std::string addonA = tokens[1] == "none" ? "" : tokens[1];
            if (!context.gameplay->SetSurvivorItemLoadout(hud.survivorItemId == "none" ? "" : hud.survivorItemId, addonA, hud.survivorItemAddonB == "none" ? "" : hud.survivorItemAddonB))
            {
                LogError("item_addon_a failed (invalid id/mismatch)");
                return;
            }
            LogSuccess("item_addon_a: " + (addonA.empty() ? "none" : addonA));
        });

        RegisterCommand("item_addon_b <id|none>", "Set survivor item addon B", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: item_addon_b <id|none>");
                return;
            }
            auto hud = context.gameplay->BuildHudState();
            const std::string addonB = tokens[1] == "none" ? "" : tokens[1];
            if (!context.gameplay->SetSurvivorItemLoadout(hud.survivorItemId == "none" ? "" : hud.survivorItemId, hud.survivorItemAddonA == "none" ? "" : hud.survivorItemAddonA, addonB))
            {
                LogError("item_addon_b failed (invalid id/mismatch)");
                return;
            }
            LogSuccess("item_addon_b: " + (addonB.empty() ? "none" : addonB));
        });

        RegisterCommand("power_addon_a <id|none>", "Set killer power addon A", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: power_addon_a <id|none>");
                return;
            }
            auto hud = context.gameplay->BuildHudState();
            const std::string addonA = tokens[1] == "none" ? "" : tokens[1];
            if (!context.gameplay->SetKillerPowerLoadout(hud.killerPowerId == "none" ? "" : hud.killerPowerId, addonA, hud.killerPowerAddonB == "none" ? "" : hud.killerPowerAddonB))
            {
                LogError("power_addon_a failed (invalid id/mismatch)");
                return;
            }
            LogSuccess("power_addon_a: " + (addonA.empty() ? "none" : addonA));
        });

        RegisterCommand("power_addon_b <id|none>", "Set killer power addon B", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: power_addon_b <id|none>");
                return;
            }
            auto hud = context.gameplay->BuildHudState();
            const std::string addonB = tokens[1] == "none" ? "" : tokens[1];
            if (!context.gameplay->SetKillerPowerLoadout(hud.killerPowerId == "none" ? "" : hud.killerPowerId, hud.killerPowerAddonA == "none" ? "" : hud.killerPowerAddonA, addonB))
            {
                LogError("power_addon_b failed (invalid id/mismatch)");
                return;
            }
            LogSuccess("power_addon_b: " + (addonB.empty() ? "none" : addonB));
        });

        RegisterCommand("addon_set_a <id|none>", "Set addon A for current role (survivor item / killer power)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: addon_set_a <id|none>");
                return;
            }
            const auto hud = context.gameplay->BuildHudState();
            const std::string addonId = tokens[1] == "none" ? "" : tokens[1];
            bool ok = false;
            if (hud.roleName == "Killer")
            {
                ok = context.gameplay->SetKillerPowerLoadout(hud.killerPowerId == "none" ? "" : hud.killerPowerId, addonId, hud.killerPowerAddonB == "none" ? "" : hud.killerPowerAddonB);
            }
            else
            {
                ok = context.gameplay->SetSurvivorItemLoadout(hud.survivorItemId == "none" ? "" : hud.survivorItemId, addonId, hud.survivorItemAddonB == "none" ? "" : hud.survivorItemAddonB);
            }
            if (ok)
            {
                LogSuccess("addon_set_a: " + (addonId.empty() ? "none" : addonId));
            }
            else
            {
                LogError("addon_set_a failed");
            }
        });

        RegisterCommand("addon_set_b <id|none>", "Set addon B for current role (survivor item / killer power)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: addon_set_b <id|none>");
                return;
            }
            const auto hud = context.gameplay->BuildHudState();
            const std::string addonId = tokens[1] == "none" ? "" : tokens[1];
            bool ok = false;
            if (hud.roleName == "Killer")
            {
                ok = context.gameplay->SetKillerPowerLoadout(hud.killerPowerId == "none" ? "" : hud.killerPowerId, hud.killerPowerAddonA == "none" ? "" : hud.killerPowerAddonA, addonId);
            }
            else
            {
                ok = context.gameplay->SetSurvivorItemLoadout(hud.survivorItemId == "none" ? "" : hud.survivorItemId, hud.survivorItemAddonA == "none" ? "" : hud.survivorItemAddonA, addonId);
            }
            if (ok)
            {
                LogSuccess("addon_set_b: " + (addonId.empty() ? "none" : addonId));
            }
            else
            {
                LogError("addon_set_b failed");
            }
        });

        RegisterCommand("item_dump", "Print survivor item loadout and runtime state", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay)
            {
                AddLog(context.gameplay->ItemDump());
            }
        });

        const auto printItemIds = [this](const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return false;
            }
            const auto ids = context.gameplay->ListItemIds();
            if (ids.empty())
            {
                AddLog("No items found");
                return false;
            }
            AddLog("Item IDs:");
            for (const auto& id : ids)
            {
                AddLog(" - " + id);
            }
            AddLog("Use: item_spawn <id> [charges], item_set <id>, item_respawn_near [radius]");
            return true;
        };

        const auto printPowerIds = [this](const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return false;
            }
            const auto ids = context.gameplay->ListPowerIds();
            if (ids.empty())
            {
                AddLog("No killer powers found");
                return false;
            }
            AddLog("Killer Power IDs:");
            for (const auto& id : ids)
            {
                AddLog(" - " + id);
            }
            AddLog("Use: power_set <id>, set_killer <characterId>");
            return true;
        };

        RegisterCommand("item_ids", "List all item IDs for item_set/item_spawn", [printItemIds](const std::vector<std::string>&, const ConsoleContext& context) {
            (void)printItemIds(context);
        });

        RegisterCommand("items", "Alias for item_ids", [printItemIds](const std::vector<std::string>&, const ConsoleContext& context) {
            (void)printItemIds(context);
        });

        RegisterCommand("list_items", "Alias for item_ids", [printItemIds](const std::vector<std::string>&, const ConsoleContext& context) {
            (void)printItemIds(context);
        });

        RegisterCommand("power_ids", "List all killer power IDs for power_set", [printPowerIds](const std::vector<std::string>&, const ConsoleContext& context) {
            (void)printPowerIds(context);
        });

        RegisterCommand("powers", "Alias for power_ids", [printPowerIds](const std::vector<std::string>&, const ConsoleContext& context) {
            (void)printPowerIds(context);
        });

        RegisterCommand("list_powers", "Alias for power_ids", [printPowerIds](const std::vector<std::string>&, const ConsoleContext& context) {
            (void)printPowerIds(context);
        });

        RegisterCommand("item_spawn <id> [charges]", "Spawn one ground item near controlled player", [this, printItemIds](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() < 2)
            {
                LogError("Usage: item_spawn <id> [charges]");
                return;
            }
            const std::string itemId = tokens[1];
            float charges = -1.0F;
            if (tokens.size() >= 3)
            {
                charges = ParseFloatOr(-1.0F, tokens[2]);
            }
            const bool ok = context.gameplay->SpawnGroundItemDebug(itemId, charges);
            if (!ok)
            {
                LogError("item_spawn failed. Valid IDs:");
                (void)printItemIds(context);
                return;
            }
            LogSuccess("item_spawn: " + itemId + (charges >= 0.0F ? (" charges=" + std::to_string(charges)) : ""));
        });

        RegisterCommand("spawn_item <id> [charges]", "Alias for item_spawn", [this, printItemIds](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() < 2)
            {
                LogError("Usage: spawn_item <id> [charges]");
                return;
            }
            const std::string itemId = tokens[1];
            float charges = -1.0F;
            if (tokens.size() >= 3)
            {
                charges = ParseFloatOr(-1.0F, tokens[2]);
            }
            const bool ok = context.gameplay->SpawnGroundItemDebug(itemId, charges);
            if (!ok)
            {
                LogError("spawn_item failed. Valid IDs:");
                (void)printItemIds(context);
                return;
            }
            LogSuccess("spawn_item: " + itemId + (charges >= 0.0F ? (" charges=" + std::to_string(charges)) : ""));
        });

        RegisterCommand("spawn_item_here <id> [charges]", "Alias for item_spawn", [this, printItemIds](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() < 2)
            {
                LogError("Usage: spawn_item_here <id> [charges]");
                return;
            }
            const std::string itemId = tokens[1];
            float charges = -1.0F;
            if (tokens.size() >= 3)
            {
                charges = ParseFloatOr(-1.0F, tokens[2]);
            }
            const bool ok = context.gameplay->SpawnGroundItemDebug(itemId, charges);
            if (!ok)
            {
                LogError("spawn_item_here failed. Valid IDs:");
                (void)printItemIds(context);
                return;
            }
            LogSuccess("spawn_item_here: " + itemId + (charges >= 0.0F ? (" charges=" + std::to_string(charges)) : ""));
        });

        RegisterCommand("power_dump", "Print killer power loadout and trap summary", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay)
            {
                AddLog(context.gameplay->PowerDump());
            }
        });

        RegisterCommand("set_survivor <id>", "Select survivor character id", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: set_survivor <id>");
                return;
            }
            if (!context.gameplay->SetSelectedSurvivorCharacter(tokens[1]))
            {
                LogError("set_survivor failed: unknown id");
                return;
            }
            LogSuccess("set_survivor: " + tokens[1]);
        });

        RegisterCommand("set_killer <id>", "Select killer character id (updates power_id)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: set_killer <id>");
                return;
            }
            if (!context.gameplay->SetSelectedKillerCharacter(tokens[1]))
            {
                LogError("set_killer failed: unknown id");
                return;
            }
            LogSuccess("set_killer: " + tokens[1]);
        });

        RegisterCommand("trap_spawn [count]", "Spawn bear trap(s) at killer forward", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            int count = 1;
            if (tokens.size() >= 2)
            {
                count = std::max(1, ParseIntOr(1, tokens[1]));
            }
            context.gameplay->TrapSpawnDebug(count);
            LogSuccess("trap_spawn: " + std::to_string(count));
        });

        RegisterCommand("item_respawn_near [radius]", "Respawn medkit/toolbox/flashlight/map around local player", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            float radius = 3.0F;
            if (tokens.size() >= 2)
            {
                radius = std::max(0.5F, ParseFloatOr(3.0F, tokens[1]));
            }
            const bool ok = context.gameplay->RespawnItemsNearPlayer(radius);
            if (ok)
            {
                LogSuccess("item_respawn_near: radius=" + std::to_string(radius));
            }
            else
            {
                LogError("item_respawn_near failed");
            }
        });

        RegisterCommand("trap_clear", "Clear all bear traps", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            context.gameplay->TrapClearDebug();
            LogSuccess("trap_clear completed");
        });

        RegisterCommand("trap_debug on|off", "Toggle trap debug draw helpers", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                LogError("Usage: trap_debug on|off");
                return;
            }
            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }
            context.gameplay->SetTrapDebug(enabled);
            LogSuccess(std::string("trap_debug ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("render_mode wireframe|filled", "Set render mode", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                LogError("Usage: render_mode wireframe|filled");
                return;
            }

            if (context.applyRenderMode)
            {
                context.applyRenderMode(tokens[1]);
                LogSuccess("Render mode: " + tokens[1]);
            }
        });

        RegisterCommand("quit", "Quit application", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay != nullptr)
            {
                context.gameplay->RequestQuit();
            }
            LogWarning("Quit requested");
        });

        RegisterCommand("set_vsync on|off", "Toggle VSync", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                LogError("Usage: set_vsync on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                LogError("Expected on|off");
                return;
            }

            if (context.vsync != nullptr)
            {
                *context.vsync = enabled;
            }
            if (context.applyVsync)
            {
                context.applyVsync(enabled);
            }
            LogSuccess(std::string("VSync ") + (enabled ? "enabled" : "disabled"));
        });

        RegisterCommand("set_fps 120", "Set FPS limit", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                LogError("Usage: set_fps <limit>");
                return;
            }

            const int fps = std::max(30, ParseIntOr(120, tokens[1]));
            if (context.fpsLimit != nullptr)
            {
                *context.fpsLimit = fps;
            }
            if (context.applyFpsLimit)
            {
                context.applyFpsLimit(fps);
            }
            LogSuccess("FPS limit: " + std::to_string(fps));
        });

        RegisterCommand("set_tick 30|60", "Set fixed simulation tick rate", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.setTickRate == nullptr)
            {
                LogError("Usage: set_tick 30|60");
                return;
            }

            const int requested = ParseIntOr(60, tokens[1]);
            const int hz = requested <= 30 ? 30 : 60;
            context.setTickRate(hz);
            LogSuccess("Fixed tick: " + std::to_string(hz) + " Hz");
        });

        RegisterCommand("set_resolution 1600 900", "Set window resolution", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 3)
            {
                LogError("Usage: set_resolution <width> <height>");
                return;
            }

            const int width = std::max(640, ParseIntOr(1600, tokens[1]));
            const int height = std::max(360, ParseIntOr(900, tokens[2]));
            if (context.applyResolution)
            {
                context.applyResolution(width, height);
            }
            LogSuccess("Resolution: " + tokens[1] + "x" + tokens[2]);
        });

        RegisterCommand("toggle_fullscreen", "Toggle fullscreen", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.toggleFullscreen)
            {
                context.toggleFullscreen();
                LogSuccess("Fullscreen toggled");
            }
        });
    }

    void ExecuteCommand(const std::string& commandLine, const ConsoleContext& context)
    {
        LogCommand("» " + commandLine);

        std::vector<std::string> tokens = Tokenize(commandLine);
        if (tokens.empty())
        {
            return;
        }

        history.erase(std::remove(history.begin(), history.end(), commandLine), history.end());
        history.push_back(commandLine);
        historyPos = -1;

        const auto it = commandRegistry.find(tokens[0]);
        if (it == commandRegistry.end())
        {
            LogError("Unknown command. Type `help` for a list of available commands");
            return;
        }

        if (tokens.size() == 2 && tokens[1] == "help")
        {
            for (const CommandInfo& info : commandInfos)
            {
                std::vector<std::string> cmdTokens = Tokenize(info.usage);
                if (!cmdTokens.empty() && cmdTokens[0] == tokens[0])
                {
                    AddLog("Usage: " + info.usage, ConsoleColors::Command);
                    AddLog("Description: " + info.description, ConsoleColors::Info);
                    return;
                }
            }
            LogError("Command exists but no help found");
            return;
        }

        it->second(tokens, context);
    }

    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
    {
        Impl* impl = static_cast<Impl*>(data->UserData);
        return impl->TextEditCallback(data);
    }

    int TextEditCallback(ImGuiInputTextCallbackData* data)
    {
        switch (data->EventFlag)
        {
            case ImGuiInputTextFlags_CallbackCompletion:
            {
                const std::string currentInput(data->Buf, data->BufTextLen);
                const std::vector<std::string> inputTokens = Tokenize(currentInput);

                const char* wordEnd = data->Buf + data->CursorPos;
                const char* wordStart = wordEnd;
                while (wordStart > data->Buf && wordStart[-1] != ' ' && wordStart[-1] != '\t')
                {
                    --wordStart;
                }

                if (inputTokens.empty())
                {
                    break;
                }

                const std::string currentWord(wordStart, wordEnd);
                const bool isCompletingCommand = (inputTokens.size() == 1 && currentWord == inputTokens[0]) || (inputTokens.empty());

                if (isCompletingCommand)
                {
                    std::vector<std::string> candidates;
                    for (const CommandInfo& info : commandInfos)
                    {
                        std::string cmdName = Tokenize(info.usage)[0];
                        if (cmdName.rfind(currentWord, 0) == 0)
                        {
                            candidates.push_back(cmdName);
                        }
                    }

                    if (candidates.empty())
                    {
                        break;
                    }

                    if (candidates.size() == 1)
                    {
                        std::string completion = candidates[0];
                        data->DeleteChars(static_cast<int>(wordStart - data->Buf), static_cast<int>(wordEnd - wordStart));
                        data->InsertChars(data->CursorPos, (completion + " ").c_str());
                    }
                    else
                    {
                        int commonLen = static_cast<int>(candidates[0].length());
                        for (std::size_t i = 1; i < candidates.size(); ++i)
                        {
                            int len = 0;
                            const std::string& a = candidates[0];
                            const std::string& b = candidates[i];
                            while (len < static_cast<int>(std::min(a.length(), b.length())) && a[len] == b[len])
                            {
                                ++len;
                            }
                            commonLen = std::min(commonLen, len);
                        }

                        if (commonLen > static_cast<int>(wordEnd - wordStart))
                        {
                            std::string completion = candidates[0].substr(0, static_cast<std::size_t>(commonLen));
                            data->DeleteChars(static_cast<int>(wordStart - data->Buf), static_cast<int>(wordEnd - wordStart));
                            data->InsertChars(data->CursorPos, completion.c_str());
                        }
                        else
                        {
                            AddLog("Possible matches:", ConsoleColors::Info);
                            for (const std::string& candidate : candidates)
                            {
                                AddLog("  • " + candidate, ConsoleColors::Value);
                            }
                        }
                    }
                }
                else
                {
                    const std::string& commandName = inputTokens[0];
                    const int paramIndex = static_cast<int>(inputTokens.size()) - 2;

                    std::vector<std::string> options = GetParamOptions(commandName, paramIndex);

                    if (!options.empty())
                    {
                        std::string currentWord(wordStart, wordEnd);
                        const bool isEmptyOrWhitespace = currentWord.empty() || currentWord.back() == ' ';

                        std::string contextKey = commandName + ":" + std::to_string(paramIndex);
                        if (lastCompletionInput != contextKey)
                        {
                            lastCompletionInput = contextKey;
                            completionCycleIndex = -1;
                            for (std::size_t i = 0; i < options.size(); ++i)
                            {
                                if (options[i] == currentWord)
                                {
                                    completionCycleIndex = static_cast<int>(i);
                                    break;
                                }
                            }
                        }

                        if (isEmptyOrWhitespace)
                        {
                            AddLog("Valid options (TAB to cycle):", glm::vec4{0.45F, 0.85F, 0.45F, 1.0F});
                            for (const std::string& opt : options)
                            {
                                AddLog("  • " + opt, glm::vec4{0.9F, 0.9F, 0.7F, 1.0F});
                            }

                            completionCycleIndex = 0;
                            data->InsertChars(data->CursorPos, options[0].c_str());
                        }
                        else
                        {
                            completionCycleIndex = (completionCycleIndex + 1) % static_cast<int>(options.size());
                            const std::string& nextOption = options[static_cast<std::size_t>(completionCycleIndex)];

                            if (!currentWord.empty())
                            {
                                data->DeleteChars(static_cast<int>(wordStart - data->Buf), static_cast<int>(wordEnd - wordStart));
                                data->InsertChars(data->CursorPos, nextOption.c_str());
                            }
                        }
                    }
                    else
                    {
                        for (const CommandInfo& info : commandInfos)
                        {
                            std::vector<std::string> tokens = Tokenize(info.usage);
                            if (!tokens.empty() && tokens[0] == commandName && tokens.size() > static_cast<std::size_t>(paramIndex + 1))
                            {
                                AddLog("Expected: " + tokens[static_cast<std::size_t>(paramIndex + 1)], glm::vec4{0.6F, 0.7F, 0.9F, 1.0F});
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case ImGuiInputTextFlags_CallbackHistory:
            {
                const int previousHistoryPos = historyPos;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (historyPos == -1)
                    {
                        historyPos = static_cast<int>(history.size()) - 1;
                    }
                    else if (historyPos > 0)
                    {
                        --historyPos;
                    }
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (historyPos != -1)
                    {
                        if (++historyPos >= static_cast<int>(history.size()))
                        {
                            historyPos = -1;
                        }
                    }
                }

                if (previousHistoryPos != historyPos)
                {
                    const char* historyText = (historyPos >= 0) ? history[static_cast<size_t>(historyPos)].c_str() : "";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, historyText);
                }
                break;
            }
            default:
                break;
        }

        return 0;
    }
};
#endif

bool DeveloperConsole::Initialize(engine::platform::Window& window)
{
#if BUILD_WITH_IMGUI
    m_impl = new Impl();
    m_impl->RegisterDefaultCommands();
    m_impl->AddLog("Developer console ready. Press ~ to toggle.", ConsoleColors::Success);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window.NativeHandle(), true);
    ImGui_ImplOpenGL3_Init("#version 450");
#else
    (void)window;
#endif
    return true;
}

void DeveloperConsole::Shutdown()
{
#if BUILD_WITH_IMGUI
    if (m_impl != nullptr)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        delete m_impl;
        m_impl = nullptr;
    }
#endif
}

void DeveloperConsole::BeginFrame()
{
#if BUILD_WITH_IMGUI
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
#endif
}

void DeveloperConsole::Render(const ConsoleContext& context, float fps, const game::gameplay::HudState& hudState)
{
#if BUILD_WITH_IMGUI
    if (m_impl == nullptr)
    {
        return;
    }

    const bool showOverlay = context.showDebugOverlay == nullptr || *context.showDebugOverlay;
    const bool showMovement = context.showMovementWindow != nullptr && *context.showMovementWindow;
    const bool showStats = context.showStatsWindow != nullptr && *context.showStatsWindow;

    if (context.renderPlayerHud)
    {
        // Movement window (controlled by toolbar button)
        if (showMovement)
        {
            bool movementOpen = *context.showMovementWindow;
            ImGui::SetNextWindowBgAlpha(0.46F);
            ImGui::SetNextWindowPos(ImVec2(10.0F, 10.0F), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Movement", &movementOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Role: %s", hudState.roleName.c_str());
                ImGui::Text("State: %s", hudState.survivorStateName.c_str());
                ImGui::Text("Move: %s", hudState.movementStateName.c_str());
                ImGui::Text("Camera: %s", hudState.cameraModeName.c_str());
                ImGui::Text("Chase: %s", hudState.chaseActive ? "ON" : "OFF");
                if (hudState.roleName == "Killer" && hudState.bloodlustTier > 0)
                {
                    ImGui::Text("Bloodlust: T%d (%.0f%% speed)", hudState.bloodlustTier, hudState.bloodlustSpeedMultiplier * 100.0F);
                }
                ImGui::Text("Render: %s", hudState.renderModeName.c_str());
                ImGui::Text("Attack: %s", hudState.killerAttackStateName.c_str());
                if (hudState.roleName == "Killer")
                {
                    ImGui::TextUnformatted(hudState.attackHint.c_str());
                }
                if (hudState.roleName == "Killer" && hudState.lungeCharge01 > 0.0F)
                {
                    ImGui::ProgressBar(hudState.lungeCharge01, ImVec2(220.0F, 0.0F), "Lunge momentum");
                }
                if (hudState.selfHealing)
                {
                    ImGui::ProgressBar(hudState.selfHealProgress, ImVec2(220.0F, 0.0F), "Self-heal");
                }
                if (hudState.roleName == "Survivor" && hudState.survivorStateName == "Carried")
                {
                    ImGui::TextUnformatted("Wiggle: Alternate A/D to escape");
                    ImGui::ProgressBar(hudState.carryEscapeProgress, ImVec2(220.0F, 0.0F), "Carry escape");
                }
                ImGui::Text("Terror Radius: %s %.1fm", hudState.terrorRadiusVisible ? "ON" : "OFF", hudState.terrorRadiusMeters);
            }
            ImGui::End();
            *context.showMovementWindow = movementOpen;
        }

        // Stats window (controlled by toolbar button)
        if (showStats)
        {
            bool statsOpen = *context.showStatsWindow;
            ImGui::SetNextWindowBgAlpha(0.46F);
            ImGui::SetNextWindowPos(ImVec2(10.0F, 165.0F), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Stats", &statsOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Generators: %d/%d", hudState.generatorsCompleted, hudState.generatorsTotal);
                if (hudState.repairingGenerator)
                {
                    ImGui::ProgressBar(hudState.activeGeneratorProgress, ImVec2(220.0F, 0.0F));
                }
                ImGui::Text("Speed: %.2f", hudState.playerSpeed);
                ImGui::Text("Grounded: %s", hudState.grounded ? "true" : "false");
                ImGui::Text("Chase: %s", hudState.chaseActive ? "ON" : "OFF");
                ImGui::Text("Distance: %.2f", hudState.chaseDistance);
                ImGui::Text("LOS: %s", hudState.lineOfSight ? "true" : "false");
                ImGui::Text("Hook Stage: %d", hudState.hookStage);
                ImGui::Text("Hook Progress: %.0f%%", hudState.hookStageProgress * 100.0F);
            }
            ImGui::End();
            *context.showStatsWindow = statsOpen;
        }

        if (!hudState.interactionPrompt.empty())
        {
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowBgAlpha(0.62F);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5F, 0.5F));
            if (ImGui::Begin("Interaction Prompt", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
            {
                ImGui::TextUnformatted(hudState.interactionPrompt.c_str());
            }
            ImGui::End();
        }

        if (hudState.skillCheckActive)
        {
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            const ImVec2 size{220.0F, 180.0F};
            ImGui::SetNextWindowBgAlpha(0.70F);
            ImGui::SetNextWindowPos(ImVec2(center.x - size.x * 0.5F, center.y + 90.0F), ImGuiCond_Always);
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
            if (ImGui::Begin("Skill Check Widget", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
            {
                ImGui::TextUnformatted("SKILL CHECK");
                ImGui::TextUnformatted("Press SPACE in green zone");

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 winPos = ImGui::GetWindowPos();
                const ImVec2 localCenter = ImVec2(winPos.x + size.x * 0.5F, winPos.y + 108.0F);
                constexpr float radius = 56.0F;

                drawList->AddCircle(localCenter, radius, IM_COL32(190, 190, 190, 255), 64, 2.0F);

                constexpr float kPi = 3.1415926535F;
                const float startAngle = -kPi * 0.5F;
                const float successStart = startAngle + hudState.skillCheckSuccessStart * 2.0F * kPi;
                const float successEnd = startAngle + hudState.skillCheckSuccessEnd * 2.0F * kPi;
                drawList->PathArcTo(localCenter, radius + 1.0F, successStart, successEnd, 28);
                drawList->PathStroke(IM_COL32(80, 220, 110, 255), false, 6.0F);

                const float needleAngle = startAngle + hudState.skillCheckNeedle * 2.0F * kPi;
                const ImVec2 needleEnd{
                    localCenter.x + std::cos(needleAngle) * (radius - 5.0F),
                    localCenter.y + std::sin(needleAngle) * (radius - 5.0F),
                };
                drawList->AddLine(localCenter, needleEnd, IM_COL32(240, 80, 80, 255), 3.0F);
                drawList->AddCircleFilled(localCenter, 4.0F, IM_COL32(240, 240, 240, 255));
            }
            ImGui::End();
        }

        if (showOverlay)
        {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowBgAlpha(0.56F);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - 12.0F, viewport->Pos.y + 12.0F),
            ImGuiCond_FirstUseEver,
            ImVec2(1.0F, 0.0F)
        );
        if (ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Role: %s", hudState.roleName.c_str());
            ImGui::Text("Camera: %s", hudState.cameraModeName.c_str());
            ImGui::Text("Render: %s", hudState.renderModeName.c_str());
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", fps);
            ImGui::Text("Speed: %.2f", hudState.playerSpeed);
            ImGui::Text("Grounded: %s", hudState.grounded ? "true" : "false");
            ImGui::Text("Chase: %s", hudState.chaseActive ? "ON" : "OFF");
            ImGui::Text("Distance: %.2f m", hudState.chaseDistance);
            ImGui::Text("LOS: %s | CenterFOV: %s", hudState.lineOfSight ? "true" : "false", hudState.inCenterFOV ? "true" : "false");
            ImGui::Text("Sprinting: %s", hudState.survivorSprinting ? "true" : "false");
            if (hudState.chaseActive)
            {
                ImGui::Text("Chase Time: %.1fs", hudState.timeInChase);
            }
            if (!hudState.lineOfSight)
            {
                ImGui::Text("Since LOS: %.1fs", hudState.timeSinceLOS);
            }
            if (!hudState.inCenterFOV)
            {
                ImGui::Text("Since CtrFOV: %.1fs", hudState.timeSinceCenterFOV);
            }
            ImGui::Text("MoveState: %s", hudState.movementStateName.c_str());
            ImGui::Text("KillerAttack: %s", hudState.killerAttackStateName.c_str());
            ImGui::Text("LungeCharge: %.0f%%", hudState.lungeCharge01 * 100.0F);
            ImGui::Text("Map: %s", hudState.mapName.c_str());
            ImGui::Text("Loop Tile: %d", hudState.activeLoopTileId);
            ImGui::Text("Loop Archetype: %s", hudState.activeLoopArchetype.c_str());
            ImGui::Text("Generators: %d/%d", hudState.generatorsCompleted, hudState.generatorsTotal);
            ImGui::Text("Survivor FSM: %s", hudState.survivorStateName.c_str());
            ImGui::Text("Carry Escape: %.0f%%", hudState.carryEscapeProgress * 100.0F);
            if (hudState.carryEscapeProgress > 0.0F)
            {
                ImGui::TextUnformatted("Wiggle: Alternate A/D");
            }
            ImGui::Text("Hook Stage: %d", hudState.hookStage);
            ImGui::Text("Hook Progress: %.0f%%", hudState.hookStageProgress * 100.0F);
            ImGui::Text("Repairing: %s", hudState.repairingGenerator ? "yes" : "no");
            ImGui::Text("Generator Progress: %.0f%%", hudState.activeGeneratorProgress * 100.0F);
            ImGui::TextUnformatted("Survivors:");
            for (const std::string& survivor : hudState.survivorStates)
            {
                ImGui::Text("  %s", survivor.c_str());
            }
            ImGui::Text("VaultType: %s", hudState.vaultTypeName.c_str());
            ImGui::Text("Interaction: %s", hudState.interactionTypeName.c_str());
            ImGui::Text("Target: %s", hudState.interactionTargetName.c_str());
            ImGui::Text("Priority: %d", hudState.interactionPriority);
            ImGui::Separator();
            ImGui::Text("FX Instances: %d", hudState.fxActiveInstances);
            ImGui::Text("FX Particles: %d", hudState.fxActiveParticles);
            ImGui::Text("FX CPU: %.3f ms", hudState.fxCpuMs);
            ImGui::Separator();
            ImGui::Text("WASD: Move");
            ImGui::Text("Mouse: Look");
            ImGui::Text("Shift: Sprint (Survivor)");
            ImGui::Text("Ctrl: Crouch (Survivor)");
            ImGui::Text("E: Interact");
            ImGui::Text("Space: Jump (N/A)");
            ImGui::Text("LMB click: Short swing (Killer)");
            ImGui::Text("Hold LMB: Lunge (Killer)");
            ImGui::Text("Space: Skill Check (Repair)");
            ImGui::Text("~: Console");
            ImGui::Text("F1/F2/F3/F4/F5: HUD/DebugDraw/RenderMode/NetDebug/TerrorRadius");
            ImGui::Text("Press ~ for Console");

            if (hudState.physicsDebugEnabled)
            {
                ImGui::Separator();
                ImGui::Text("Velocity: (%.2f, %.2f, %.2f)", hudState.velocity.x, hudState.velocity.y, hudState.velocity.z);
                ImGui::Text("Last Normal: (%.2f, %.2f, %.2f)", hudState.lastCollisionNormal.x, hudState.lastCollisionNormal.y, hudState.lastCollisionNormal.z);
                ImGui::Text("Penetration: %.4f", hudState.penetrationDepth);
            }
        }
        ImGui::End();

        if (!hudState.runtimeMessage.empty())
        {
            ImGui::SetNextWindowBgAlpha(0.45F);
            ImGui::SetNextWindowPos(ImVec2(0.0F, 48.0F), ImGuiCond_Always, ImVec2(0.5F, 0.0F));
            if (ImGui::Begin("RuntimeMsg", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
            {
                ImGui::TextUnformatted(hudState.runtimeMessage.c_str());
            }
            ImGui::End();
        }

        // Debug overlay: always draw perks at top-left when F2 is active
        if (context.gameplay != nullptr && !hudState.debugActors.empty())
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float aspect = viewport->Size.y > 1.0F ? viewport->Size.x / viewport->Size.y : (16.0F / 9.0F);
            const glm::mat4 viewProjection = context.gameplay->BuildViewProjection(aspect);
            ImDrawList* drawList = ImGui::GetForegroundDrawList();

            auto projectWorld = [&](const glm::vec3& world, ImVec2& outScreen) {
                const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0F);
                if (clip.w <= 0.01F)
                {
                    return false;
                }
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (ndc.x < -1.2F || ndc.x > 1.2F || ndc.y < -1.2F || ndc.y > 1.2F)
                {
                    return false;
                }
                outScreen.x = viewport->Pos.x + (ndc.x * 0.5F + 0.5F) * viewport->Size.x;
                outScreen.y = viewport->Pos.y + (1.0F - (ndc.y * 0.5F + 0.5F)) * viewport->Size.y;
                return true;
            };

            for (const auto& actor : hudState.debugActors)
            {
                ImVec2 screen{};
                if (!projectWorld(actor.worldPosition, screen))
                {
                    continue;
                }

                ImU32 labelColor = actor.killer ? IM_COL32(255, 120, 120, 255) : IM_COL32(120, 255, 120, 255);
                const std::string line1 = actor.name + (actor.chasing ? " [CHASE]" : "");
                const std::string line2 = "HP:" + actor.healthState + " MOV:" + actor.movementState + " SPD:" + std::to_string(actor.speed);
                const std::string line3 = actor.killer ? ("ATK:" + actor.attackState) : "";
                drawList->AddText(ImVec2(screen.x - 84.0F, screen.y - 30.0F), labelColor, line1.c_str());
                drawList->AddText(ImVec2(screen.x - 84.0F, screen.y - 16.0F), IM_COL32(235, 235, 235, 255), line2.c_str());
                if (!line3.empty())
                {
                    drawList->AddText(ImVec2(screen.x - 84.0F, screen.y - 2.0F), IM_COL32(255, 210, 120, 255), line3.c_str());
                }

                glm::vec3 forward = actor.forward;
                if (glm::length(forward) < 1.0e-5F)
                {
                    forward = glm::vec3{0.0F, 0.0F, -1.0F};
                }
                forward = glm::normalize(forward);
                ImVec2 endScreen{};
                if (projectWorld(actor.worldPosition + glm::vec3{forward.x, 0.0F, forward.z} * 1.3F, endScreen))
                {
                    drawList->AddLine(screen, endScreen, IM_COL32(80, 180, 255, 230), 2.0F);
                }
            }
        }

        // Render perk debug info (top-left corner below runtime message) - always when debug mode is on
        if (context.gameplay != nullptr && !hudState.debugActors.empty())
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* drawList = ImGui::GetForegroundDrawList();

            // Render perk debug info (top-left corner below runtime message)
            const ImVec2 perkOrigin(viewport->Pos.x + 16.0F, viewport->Pos.y + 88.0F);
            const float lineHeight = 18.0F;
            float perkY = perkOrigin.y;
            
            const auto drawPerkSection = [&](const std::string& label, const std::vector<game::gameplay::HudState::ActivePerkDebug>& perks, float mod) {
                drawList->AddText(perkOrigin, IM_COL32(200, 200, 200, 255), (label + " (x" + std::to_string(mod).substr(0, 4) + ")").c_str());
                perkY += lineHeight;
                
                if (perks.empty())
                {
                    drawList->AddText(ImVec2(perkOrigin.x, perkY), IM_COL32(120, 120, 120, 255), "  [none]");
                    perkY += lineHeight;
                }
                else
                {
                    for (const auto& perk : perks)
                    {
                        const ImU32 color = perk.isActive ? IM_COL32(120, 255, 120, 255) : IM_COL32(180, 180, 180, 255);
                        const std::string status = perk.isActive ? "ACTIVE" : "PASSIVE";
                        std::string extra;
                        if (perk.isActive && perk.activeRemainingSeconds > 0.01F)
                        {
                            extra = " (" + std::to_string(perk.activeRemainingSeconds).substr(0, 3) + "s)";
                        }
                        if (!perk.isActive && perk.cooldownRemainingSeconds > 0.01F)
                        {
                            extra = " (CD " + std::to_string(perk.cooldownRemainingSeconds).substr(0, 3) + "s)";
                        }
                        drawList->AddText(ImVec2(perkOrigin.x, perkY), color, ("  " + perk.name + " [" + status + "]" + extra).c_str());
                        perkY += lineHeight;
                    }
                }
                perkY += 8.0F; // spacing between sections
            };
            
            drawPerkSection("SURVIVOR PERKS", hudState.activePerksSurvivor, hudState.speedModifierSurvivor);
            drawPerkSection("KILLER PERKS", hudState.activePerksKiller, hudState.speedModifierKiller);
        }

        }
    }

    if (m_impl->open)
    {
        if (!m_impl->firstOpenAnnouncementDone)
        {
            m_impl->AddLog("Type `help` to list commands.", ConsoleColors::Info);
            m_impl->PrintHelp();
            m_impl->firstOpenAnnouncementDone = true;
        }

        ImGui::SetNextWindowSize(ImVec2(840.0F, 390.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Developer Console", &m_impl->open, ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoNavInputs))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsWindowFocused())
            {
                m_impl->open = false;
            }

            // Console output with colors
            // Reserve extra space for input + 2 hint lines at bottom (no scrolling needed for hints)
            constexpr float kHintLinesHeight = 50.0F; // Space for hints below input
            ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - kHintLinesHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& entry : m_impl->items)
            {
                ImU32 color = IM_COL32(255, 255, 255, 255);
                if (entry.color.r >= 0.0F && entry.color.g >= 0.0F && entry.color.b >= 0.0F && entry.color.a >= 0.0F)
                {
                    color = IM_COL32(
                        static_cast<int>(entry.color.r * 255.0F),
                        static_cast<int>(entry.color.g * 255.0F),
                        static_cast<int>(entry.color.b * 255.0F),
                        static_cast<int>(entry.color.a * 255.0F)
                    );
                }

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(entry.color.r, entry.color.g, entry.color.b, entry.color.a));
                ImGui::TextUnformatted(entry.text.c_str());
                ImGui::PopStyleColor();
            }
            if (m_impl->scrollToBottom)
            {
                ImGui::SetScrollHereY(1.0F);
                m_impl->scrollToBottom = false;
            }
            ImGui::EndChild();

            if (m_impl->reclaimFocus)
            {
            ImGui::SetKeyboardFocusHere();
            m_impl->reclaimFocus = false;
            }

            ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                             ImGuiInputTextFlags_CallbackCompletion |
                                             ImGuiInputTextFlags_CallbackHistory;
            if (ImGui::InputText(
                    "Input",
                    m_impl->inputBuffer.data(),
                    m_impl->inputBuffer.size(),
                    inputFlags,
                    &Impl::TextEditCallbackStub,
                    m_impl))
            {
                const std::string command = m_impl->inputBuffer.data();
                if (!command.empty())
                {
                    m_impl->ExecuteCommand(command, context);
                }
                m_impl->inputBuffer.fill('\0');
                m_impl->reclaimFocus = true;
            }

            const std::string currentInput = m_impl->inputBuffer.data();
            const std::vector<std::string> inputTokens = Tokenize(currentInput);

            if (!inputTokens.empty())
            {
                const std::string& commandName = inputTokens[0];
                int paramIndex = static_cast<int>(inputTokens.size()) - 1;

                std::vector<std::string> options = m_impl->GetParamOptions(commandName, paramIndex);

                if (!options.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45F, 0.85F, 0.45F, 1.0F));
                    std::string hint = "Valid options (TAB to cycle): ";
                    for (std::size_t i = 0; i < options.size(); ++i)
                    {
                        if (i > 0) hint += " | ";
                        hint += options[i];
                    }
                    ImGui::TextUnformatted(hint.c_str());
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6F, 0.6F, 0.7F, 1.0F));
                    ImGui::TextUnformatted("Hint: TAB autocomplete | ESC close | UP/DOWN history | clear to clean");
                    ImGui::PopStyleColor();
                }
            }
            else if (currentInput.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6F, 0.6F, 0.7F, 1.0F));
                ImGui::TextUnformatted("Hint: TAB autocomplete | ESC close | UP/DOWN history | clear to clean");
                ImGui::PopStyleColor();
            }
            else
            {
                const std::vector<Impl::CommandInfo> hints = m_impl->BuildHints(currentInput);
                if (!hints.empty())
                {
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5F, 0.7F, 0.9F, 1.0F));
                    ImGui::TextUnformatted("Suggestions:");
                    ImGui::PopStyleColor();
                    const int maxHints = std::min<int>(8, static_cast<int>(hints.size()));
                    for (int i = 0; i < maxHints; ++i)
                    {
                        const auto& hint = hints[static_cast<std::size_t>(i)];
                        ImVec4 catColor = ImVec4(0.55F, 0.85F, 0.95F, 1.0F);
                        ImVec4 usageColor = ImVec4(0.4F, 0.8F, 1.0F, 1.0F);
                        ImVec4 descColor = ImVec4(0.75F, 0.75F, 0.8F, 1.0F);

                        ImGui::PushStyleColor(ImGuiCol_Text, catColor);
                        ImGui::Text("[%s]", hint.category.c_str());
                        ImGui::SameLine(0.0F, 0.0F);
                        ImGui::PopStyleColor();

                        ImGui::SameLine(0.0F, 4.0F);
                        ImGui::PushStyleColor(ImGuiCol_Text, usageColor);
                        ImGui::Text(" %s", hint.usage.c_str());
                        ImGui::SameLine(0.0F, 0.0F);
                        ImGui::PopStyleColor();

                        ImGui::SameLine(0.0F, 6.0F);
                        ImGui::PushStyleColor(ImGuiCol_Text, descColor);
                        ImGui::TextUnformatted(" — ");
                        ImGui::SameLine(0.0F, 0.0F);
                        ImGui::TextUnformatted(hint.description.c_str());
                        ImGui::PopStyleColor();
                    }
                }
            }
        }
        ImGui::End();
    }

    // Perks HUD - only visible when in game and loadout has perks
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float screenHeight = viewport->Size.y;
        const float perkHudY = screenHeight - 160.0F;
        const bool isKiller = hudState.roleName == "Killer";

        // Check if any perks are equipped
        const auto& slots = isKiller ? hudState.killerPerkSlots : hudState.survivorPerkSlots;
        const bool hasAnyPerk = std::any_of(slots.begin(), slots.end(), 
            [](const game::gameplay::HudState::ActivePerkDebug& p) { return !p.id.empty(); });

        // Only render if in game AND at least one perk is equipped
        if (hudState.isInGame && hasAnyPerk)
        {
            if (isKiller)
            {
                RenderPerkSlotHud(hudState.killerPerkSlots, ImVec2(viewport->Size.x - 18.0F, perkHudY), true, true);
            }
            else
            {
                RenderPerkSlotHud(hudState.survivorPerkSlots, ImVec2(18.0F, perkHudY), false, false);
            }
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#else
    (void)context;
    (void)fps;
    (void)hudState;
#endif
}

void DeveloperConsole::Toggle()
{
#if BUILD_WITH_IMGUI
    if (m_impl != nullptr)
    {
        const bool wasOpen = m_impl->open;
        m_impl->open = !m_impl->open;
        // Set focus only when opening, not when closing
        if (!wasOpen && m_impl->open)
        {
            m_impl->reclaimFocus = true;
        }
    }
#endif
}

bool DeveloperConsole::IsOpen() const
{
#if BUILD_WITH_IMGUI
    return m_impl != nullptr && m_impl->open;
#else
    return false;
#endif
}

bool DeveloperConsole::WantsKeyboardCapture() const
{
#if BUILD_WITH_IMGUI
    if (m_impl == nullptr)
    {
        return false;
    }
    return m_impl->open && ImGui::GetIO().WantCaptureKeyboard;
#else
    return false;
#endif
}
} // namespace ui
