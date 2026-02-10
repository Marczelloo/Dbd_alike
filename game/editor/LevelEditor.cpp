#include "game/editor/LevelEditor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>

#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#if BUILD_WITH_IMGUI
#include <imgui.h>
#endif

namespace game::editor
{
namespace
{
float ClampPitch(float value)
{
    return glm::clamp(value, -1.5F, 1.5F);
}

glm::vec3 RotateY(const glm::vec3& value, float degrees)
{
    const float radians = glm::radians(degrees);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::vec3{
        value.x * c - value.z * s,
        value.y,
        value.x * s + value.z * c,
    };
}

glm::mat3 RotationMatrixFromEulerDegrees(const glm::vec3& eulerDegrees)
{
    glm::mat4 transform{1.0F};
    transform = glm::rotate(transform, glm::radians(eulerDegrees.y), glm::vec3{0.0F, 1.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.x), glm::vec3{1.0F, 0.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.z), glm::vec3{0.0F, 0.0F, 1.0F});
    return glm::mat3(transform);
}

glm::vec3 RotateExtentsXYZ(const glm::vec3& halfExtents, const glm::vec3& eulerDegrees)
{
    const glm::mat3 rotation = RotationMatrixFromEulerDegrees(eulerDegrees);
    const glm::mat3 absRotation{
        glm::abs(rotation[0]),
        glm::abs(rotation[1]),
        glm::abs(rotation[2]),
    };
    return absRotation * halfExtents;
}

glm::ivec2 RotatedFootprintFor(const LoopAsset& loop, int rotationDegrees)
{
    const int rot = ((rotationDegrees % 360) + 360) % 360;
    const bool swap = rot == 90 || rot == 270;
    return glm::ivec2{
        swap ? std::max(1, loop.footprintHeight) : std::max(1, loop.footprintWidth),
        swap ? std::max(1, loop.footprintWidth) : std::max(1, loop.footprintHeight),
    };
}

bool SegmentIntersectsAabb(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const glm::vec3& minBounds,
    const glm::vec3& maxBounds,
    float* outT
)
{
    float tMin = 0.0F;
    float tMax = 10000.0F;

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) < 1.0e-7F)
        {
            if (origin[axis] < minBounds[axis] || origin[axis] > maxBounds[axis])
            {
                return false;
            }
            continue;
        }

        const float invDir = 1.0F / direction[axis];
        float t1 = (minBounds[axis] - origin[axis]) * invDir;
        float t2 = (maxBounds[axis] - origin[axis]) * invDir;
        if (t1 > t2)
        {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax)
        {
            return false;
        }
    }

    if (outT != nullptr)
    {
        *outT = tMin;
    }
    return true;
}

const char* ModeToText(LevelEditor::Mode mode)
{
    return mode == LevelEditor::Mode::LoopEditor ? "Loop Editor" : "Map Editor";
}

const char* GizmoToText(LevelEditor::GizmoMode mode)
{
    switch (mode)
    {
        case LevelEditor::GizmoMode::Translate: return "Translate";
        case LevelEditor::GizmoMode::Rotate: return "Rotate";
        case LevelEditor::GizmoMode::Scale: return "Scale";
        default: return "Translate";
    }
}

const char* PropToText(PropType type)
{
    switch (type)
    {
        case PropType::Rock: return "Rock";
        case PropType::Tree: return "Tree";
        case PropType::Obstacle: return "Obstacle";
        case PropType::Platform: return "Platform";
        case PropType::MeshAsset: return "MeshAsset";
        default: return "Rock";
    }
}

const char* LightTypeToText(LightType type)
{
    switch (type)
    {
        case LightType::Spot: return "Spot";
        case LightType::Point:
        default: return "Point";
    }
}

const char* LoopElementTypeToText(LoopElementType type)
{
    switch (type)
    {
        case LoopElementType::Wall: return "Wall";
        case LoopElementType::Window: return "Window";
        case LoopElementType::Pallet: return "Pallet";
        case LoopElementType::Marker: return "Marker";
        default: return "Wall";
    }
}

std::string QuickLoopAssetId(LoopElementType type)
{
    switch (type)
    {
        case LoopElementType::Wall: return "__quick_loop_wall";
        case LoopElementType::Window: return "__quick_loop_window";
        case LoopElementType::Pallet: return "__quick_loop_pallet";
        case LoopElementType::Marker: return "__quick_loop_marker";
        default: return "__quick_loop_wall";
    }
}

glm::vec3 QuickLoopDefaultHalfExtents(LoopElementType type)
{
    switch (type)
    {
        case LoopElementType::Wall: return glm::vec3{2.5F, 1.1F, 0.25F};
        case LoopElementType::Window: return glm::vec3{1.1F, 1.0F, 0.20F};
        case LoopElementType::Pallet: return glm::vec3{1.25F, 0.85F, 0.25F};
        case LoopElementType::Marker: return glm::vec3{0.25F, 0.25F, 0.25F};
        default: return glm::vec3{1.0F, 1.0F, 0.2F};
    }
}

glm::vec3 ElementRotation(const LoopElement& element)
{
    return glm::vec3{element.pitchDegrees, element.yawDegrees, element.rollDegrees};
}

glm::vec3 PropRotation(const PropInstance& prop)
{
    return glm::vec3{prop.pitchDegrees, prop.yawDegrees, prop.rollDegrees};
}

const char* RenderModeToText(engine::render::RenderMode mode)
{
    return mode == engine::render::RenderMode::Wireframe ? "Wireframe" : "Filled";
}

const char* AssetKindToText(engine::assets::AssetKind kind)
{
    switch (kind)
    {
        case engine::assets::AssetKind::Mesh: return "Mesh";
        case engine::assets::AssetKind::Texture: return "Texture";
        case engine::assets::AssetKind::Material: return "Material";
        case engine::assets::AssetKind::Animation: return "Animation";
        case engine::assets::AssetKind::Environment: return "Environment";
        case engine::assets::AssetKind::Prefab: return "Prefab";
        case engine::assets::AssetKind::Loop: return "Loop";
        case engine::assets::AssetKind::Map: return "Map";
        default: return "Unknown";
    }
}

engine::render::EnvironmentSettings ToRenderEnvironment(const EnvironmentAsset& env)
{
    engine::render::EnvironmentSettings settings;
    settings.skyEnabled = true;
    settings.skyTopColor = env.skyTopColor;
    settings.skyBottomColor = env.skyBottomColor;
    settings.cloudsEnabled = env.cloudsEnabled;
    settings.cloudCoverage = env.cloudCoverage;
    settings.cloudDensity = env.cloudDensity;
    settings.cloudSpeed = env.cloudSpeed;
    settings.directionalLightDirection = env.directionalLightDirection;
    settings.directionalLightColor = env.directionalLightColor;
    settings.directionalLightIntensity = env.directionalLightIntensity;
    settings.fogEnabled = env.fogEnabled;
    settings.fogColor = env.fogColor;
    settings.fogDensity = env.fogDensity;
    settings.fogStart = env.fogStart;
    settings.fogEnd = env.fogEnd;
    return settings;
}

bool SampleAnimation(const AnimationClipAsset& clip, float time, glm::vec3* outPos, glm::vec3* outRot, glm::vec3* outScale)
{
    if (clip.keyframes.empty())
    {
        return false;
    }

    if (clip.keyframes.size() == 1)
    {
        if (outPos != nullptr)
        {
            *outPos = clip.keyframes.front().position;
        }
        if (outRot != nullptr)
        {
            *outRot = clip.keyframes.front().rotationEuler;
        }
        if (outScale != nullptr)
        {
            *outScale = clip.keyframes.front().scale;
        }
        return true;
    }

    const float endTime = std::max(clip.keyframes.back().time, 0.001F);
    float sampleTime = time;
    if (clip.loop)
    {
        sampleTime = std::fmod(sampleTime, endTime);
        if (sampleTime < 0.0F)
        {
            sampleTime += endTime;
        }
    }
    else
    {
        sampleTime = glm::clamp(sampleTime, 0.0F, endTime);
    }

    std::size_t nextIdx = 0;
    for (std::size_t i = 0; i < clip.keyframes.size(); ++i)
    {
        if (clip.keyframes[i].time >= sampleTime)
        {
            nextIdx = i;
            break;
        }
        nextIdx = i;
    }

    if (nextIdx == 0)
    {
        if (outPos != nullptr)
        {
            *outPos = clip.keyframes.front().position;
        }
        if (outRot != nullptr)
        {
            *outRot = clip.keyframes.front().rotationEuler;
        }
        if (outScale != nullptr)
        {
            *outScale = clip.keyframes.front().scale;
        }
        return true;
    }

    const std::size_t prevIdx = nextIdx - 1;
    const AnimationKeyframe& a = clip.keyframes[prevIdx];
    const AnimationKeyframe& b = clip.keyframes[nextIdx];
    const float denom = std::max(0.0001F, b.time - a.time);
    const float t = glm::clamp((sampleTime - a.time) / denom, 0.0F, 1.0F);

    if (outPos != nullptr)
    {
        *outPos = glm::mix(a.position, b.position, t);
    }
    if (outRot != nullptr)
    {
        *outRot = glm::mix(a.rotationEuler, b.rotationEuler, t);
    }
    if (outScale != nullptr)
    {
        *outScale = glm::mix(a.scale, b.scale, t);
    }
    return true;
}

bool ContainsCaseInsensitive(const std::string& text, const std::string& needle)
{
    if (needle.empty())
    {
        return true;
    }

    auto lower = [](const std::string& value) {
        std::string out = value;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };

    return lower(text).find(lower(needle)) != std::string::npos;
}

std::string StripNumericSuffix(const std::string& value)
{
    if (value.empty())
    {
        return "element";
    }

    const std::size_t underscore = value.find_last_of('_');
    if (underscore == std::string::npos || underscore + 1 >= value.size())
    {
        return value;
    }

    bool digitsOnly = true;
    for (std::size_t i = underscore + 1; i < value.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(value[i])))
        {
            digitsOnly = false;
            break;
        }
    }
    if (!digitsOnly)
    {
        return value;
    }

    const std::string base = value.substr(0, underscore);
    return base.empty() ? "element" : base;
}
} // namespace

void LevelEditor::Initialize()
{
    LevelAssetIO::EnsureAssetDirectories();
    m_assetRegistry.EnsureAssetDirectories();
    RefreshLibraries();
    RefreshContentBrowser();
    CreateNewLoop();
    CreateNewMap();
    if (LevelAssetIO::ListEnvironmentIds().empty())
    {
        EnvironmentAsset defaultEnv;
        defaultEnv.id = "default_environment";
        defaultEnv.displayName = "Default Environment";
        std::string error;
        (void)LevelAssetIO::SaveEnvironment(defaultEnv, &error);
    }
    std::string envError;
    if (!LevelAssetIO::LoadEnvironment("default_environment", &m_environmentEditing, &envError))
    {
        m_environmentEditing = EnvironmentAsset{};
    }
    m_materialEditing = MaterialAsset{};
    m_materialEditing.id = "new_material";
    m_materialEditing.displayName = "New Material";
    m_animationEditing = AnimationClipAsset{};
    m_animationEditing.id = "new_clip";
    m_animationEditing.displayName = "New Clip";
    m_materialCache.clear();
    m_animationCache.clear();
    m_undoStack.clear();
    m_redoStack.clear();
}

void LevelEditor::Enter(Mode mode)
{
    m_mode = mode;
    ClearSelections();
    m_propPlacementMode = false;
    m_pendingPlacementRotation = 0;
    m_axisDragActive = false;
    m_axisDragAxis = GizmoAxis::None;
    m_axisDragMode = GizmoMode::Translate;
    m_gizmoEditing = false;

    if (mode == Mode::LoopEditor)
    {
        // Keep loop editing deterministic: always start focused on a single 16x16 tile area.
        m_topDownView = false;
        m_cameraPosition = glm::vec3{0.0F, 11.0F, 18.0F};
        m_cameraYaw = 0.0F;
        m_cameraPitch = -0.52F;
        m_cameraSpeed = 16.0F;
        m_debugView = true;
    }

    m_contentNeedsRefresh = true;
    m_statusLine = std::string{"Entered "} + ModeToText(mode);
}

std::optional<engine::render::RenderMode> LevelEditor::ConsumeRequestedRenderMode()
{
    const std::optional<engine::render::RenderMode> mode = m_pendingRenderMode;
    m_pendingRenderMode.reset();
    return mode;
}

glm::vec3 LevelEditor::CameraForward() const
{
    if (m_topDownView)
    {
        return glm::vec3{0.0F, -1.0F, 0.0F};
    }

    const float cosPitch = std::cos(m_cameraPitch);
    return glm::normalize(glm::vec3{
        std::sin(m_cameraYaw) * cosPitch,
        std::sin(m_cameraPitch),
        -std::cos(m_cameraYaw) * cosPitch,
    });
}

glm::vec3 LevelEditor::CameraUp() const
{
    if (m_topDownView)
    {
        // Forward=(0,-1,0) cannot use world up=(0,1,0) in lookAt; pick a stable horizontal up axis.
        return glm::vec3{0.0F, 0.0F, -1.0F};
    }
    return glm::vec3{0.0F, 1.0F, 0.0F};
}

glm::vec3 LevelEditor::CameraRight() const
{
    const glm::vec3 forward = CameraForward();
    glm::vec3 right = glm::cross(forward, CameraUp());
    if (glm::length(right) < 1.0e-5F)
    {
        right = glm::vec3{1.0F, 0.0F, 0.0F};
    }
    return glm::normalize(right);
}

void LevelEditor::RefreshLibraries()
{
    m_loopLibrary = LevelAssetIO::ListLoopIds();
    m_mapLibrary = LevelAssetIO::ListMapNames();
    m_prefabLibrary = LevelAssetIO::ListPrefabIds();
    m_materialLibrary = LevelAssetIO::ListMaterialIds();
    m_animationLibrary = LevelAssetIO::ListAnimationClipIds();
    if (m_paletteLoopIndex >= static_cast<int>(m_loopLibrary.size()))
    {
        m_paletteLoopIndex = m_loopLibrary.empty() ? -1 : 0;
    }
    if (m_selectedPrefabIndex >= static_cast<int>(m_prefabLibrary.size()))
    {
        m_selectedPrefabIndex = m_prefabLibrary.empty() ? -1 : 0;
    }
    if (m_selectedMaterialIndex >= static_cast<int>(m_materialLibrary.size()))
    {
        m_selectedMaterialIndex = m_materialLibrary.empty() ? -1 : 0;
    }
    if (m_selectedAnimationIndex >= static_cast<int>(m_animationLibrary.size()))
    {
        m_selectedAnimationIndex = m_animationLibrary.empty() ? -1 : 0;
    }

    if (m_selectedMaterialIndex >= 0 && m_selectedMaterialIndex < static_cast<int>(m_materialLibrary.size()))
    {
        m_selectedMaterialId = m_materialLibrary[static_cast<std::size_t>(m_selectedMaterialIndex)];
    }
    else
    {
        m_selectedMaterialId.clear();
    }

    if (m_selectedAnimationIndex >= 0 && m_selectedAnimationIndex < static_cast<int>(m_animationLibrary.size()))
    {
        m_animationPreviewClip = m_animationLibrary[static_cast<std::size_t>(m_selectedAnimationIndex)];
    }
    else
    {
        m_animationPreviewClip.clear();
    }
}

void LevelEditor::CreateNewLoop(const std::string& suggestedName)
{
    m_loop = LoopAsset{};
    m_loop.id = suggestedName;
    m_loop.displayName = suggestedName;
    m_loop.elements.clear();
    ClearSelections();
}

void LevelEditor::CreateNewMap(const std::string& suggestedName)
{
    m_map = MapAsset{};
    m_map.name = suggestedName;
    m_map.environmentAssetId = "default_environment";
    m_map.placements.clear();
    m_map.props.clear();
    m_selectedLightIndex = -1;
    (void)LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr);
    ClearSelections();
}

LevelEditor::HistoryState LevelEditor::CaptureHistoryState() const
{
    HistoryState state;
    state.mode = m_mode;
    state.loop = m_loop;
    state.map = m_map;
    state.selection = m_selection;
    state.selectedLoopElements = m_selectedLoopElements;
    state.selectedMapPlacements = m_selectedMapPlacements;
    state.selectedProps = m_selectedProps;
    state.propPlacementMode = m_propPlacementMode;
    state.pendingPlacementRotation = m_pendingPlacementRotation;
    state.paletteLoopIndex = m_paletteLoopIndex;
    state.selectedPropType = m_selectedPropType;
    return state;
}

void LevelEditor::RestoreHistoryState(const HistoryState& state)
{
    m_historyApplying = true;
    m_mode = state.mode;
    m_loop = state.loop;
    m_map = state.map;
    m_selection = state.selection;
    m_selectedLoopElements = state.selectedLoopElements;
    m_selectedMapPlacements = state.selectedMapPlacements;
    m_selectedProps = state.selectedProps;
    m_propPlacementMode = state.propPlacementMode;
    m_pendingPlacementRotation = state.pendingPlacementRotation;
    m_paletteLoopIndex = state.paletteLoopIndex;
    m_selectedPropType = state.selectedPropType;
    m_historyApplying = false;
}

void LevelEditor::PushHistorySnapshot()
{
    if (m_historyApplying)
    {
        return;
    }

    m_undoStack.push_back(CaptureHistoryState());
    if (m_undoStack.size() > m_historyMaxEntries)
    {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

void LevelEditor::Undo()
{
    if (m_undoStack.empty())
    {
        m_statusLine = "Undo: no history";
        return;
    }

    m_redoStack.push_back(CaptureHistoryState());
    const HistoryState previous = m_undoStack.back();
    m_undoStack.pop_back();
    RestoreHistoryState(previous);
    m_statusLine = "Undo";
}

void LevelEditor::Redo()
{
    if (m_redoStack.empty())
    {
        m_statusLine = "Redo: no history";
        return;
    }

    m_undoStack.push_back(CaptureHistoryState());
    const HistoryState next = m_redoStack.back();
    m_redoStack.pop_back();
    RestoreHistoryState(next);
    m_statusLine = "Redo";
}

void LevelEditor::ClearSelections()
{
    m_selection = Selection{};
    m_selectedLoopElements.clear();
    m_selectedMapPlacements.clear();
    m_selectedProps.clear();
}

void LevelEditor::SelectSingle(const Selection& selection)
{
    ClearSelections();
    if (selection.kind == SelectionKind::None || selection.index < 0)
    {
        return;
    }

    m_selection = selection;
    if (selection.kind == SelectionKind::LoopElement)
    {
        m_selectedLoopElements.push_back(selection.index);
    }
    else if (selection.kind == SelectionKind::MapPlacement)
    {
        m_selectedMapPlacements.push_back(selection.index);
    }
    else if (selection.kind == SelectionKind::Prop)
    {
        m_selectedProps.push_back(selection.index);
    }
}

void LevelEditor::ToggleSelection(const Selection& selection)
{
    if (selection.kind == SelectionKind::None || selection.index < 0)
    {
        return;
    }

    std::vector<int>* list = nullptr;
    if (selection.kind == SelectionKind::LoopElement)
    {
        list = &m_selectedLoopElements;
    }
    else if (selection.kind == SelectionKind::MapPlacement)
    {
        list = &m_selectedMapPlacements;
    }
    else if (selection.kind == SelectionKind::Prop)
    {
        list = &m_selectedProps;
    }
    if (list == nullptr)
    {
        return;
    }

    if (m_selection.kind != selection.kind)
    {
        SelectSingle(selection);
        return;
    }

    const auto it = std::find(list->begin(), list->end(), selection.index);
    if (it != list->end())
    {
        list->erase(it);
        if (m_selection.kind == selection.kind && m_selection.index == selection.index)
        {
            if (list->empty())
            {
                m_selection = Selection{};
            }
            else
            {
                m_selection.index = list->front();
            }
        }
    }
    else
    {
        list->push_back(selection.index);
        m_selection = selection;
    }
}

bool LevelEditor::IsSelected(SelectionKind kind, int index) const
{
    if (index < 0)
    {
        return false;
    }

    if (kind == SelectionKind::LoopElement)
    {
        return std::find(m_selectedLoopElements.begin(), m_selectedLoopElements.end(), index) != m_selectedLoopElements.end();
    }
    if (kind == SelectionKind::MapPlacement)
    {
        return std::find(m_selectedMapPlacements.begin(), m_selectedMapPlacements.end(), index) != m_selectedMapPlacements.end();
    }
    if (kind == SelectionKind::Prop)
    {
        return std::find(m_selectedProps.begin(), m_selectedProps.end(), index) != m_selectedProps.end();
    }
    return false;
}

std::vector<int> LevelEditor::SortedUniqueValidSelection(SelectionKind kind) const
{
    std::vector<int> indices;
    if (kind == SelectionKind::LoopElement)
    {
        indices = m_selectedLoopElements;
        if (indices.empty() && m_selection.kind == kind)
        {
            indices.push_back(m_selection.index);
        }
        const int maxIndex = static_cast<int>(m_loop.elements.size());
        indices.erase(
            std::remove_if(indices.begin(), indices.end(), [maxIndex](int idx) { return idx < 0 || idx >= maxIndex; }),
            indices.end()
        );
    }
    else if (kind == SelectionKind::MapPlacement)
    {
        indices = m_selectedMapPlacements;
        if (indices.empty() && m_selection.kind == kind)
        {
            indices.push_back(m_selection.index);
        }
        const int maxIndex = static_cast<int>(m_map.placements.size());
        indices.erase(
            std::remove_if(indices.begin(), indices.end(), [maxIndex](int idx) { return idx < 0 || idx >= maxIndex; }),
            indices.end()
        );
    }
    else if (kind == SelectionKind::Prop)
    {
        indices = m_selectedProps;
        if (indices.empty() && m_selection.kind == kind)
        {
            indices.push_back(m_selection.index);
        }
        const int maxIndex = static_cast<int>(m_map.props.size());
        indices.erase(
            std::remove_if(indices.begin(), indices.end(), [maxIndex](int idx) { return idx < 0 || idx >= maxIndex; }),
            indices.end()
        );
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

void LevelEditor::HandleCamera(float deltaSeconds, const engine::platform::Input& input, bool controlsEnabled)
{
    if (!controlsEnabled)
    {
        return;
    }

    const bool lookActive = input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT);
    if (lookActive)
    {
        const glm::vec2 mouseDelta = input.MouseDelta();
        m_cameraYaw += mouseDelta.x * 0.0025F;
        m_cameraPitch = ClampPitch(m_cameraPitch - mouseDelta.y * 0.0025F);
    }

    glm::vec3 movement{0.0F};
    const glm::vec3 forward = m_topDownView ? glm::vec3{0.0F, 0.0F, -1.0F} : glm::normalize(glm::vec3{CameraForward().x, 0.0F, CameraForward().z});
    const glm::vec3 right = m_topDownView ? glm::vec3{1.0F, 0.0F, 0.0F} : CameraRight();

    if (input.IsKeyDown(GLFW_KEY_W))
    {
        movement += forward;
    }
    if (input.IsKeyDown(GLFW_KEY_S))
    {
        movement -= forward;
    }
    if (input.IsKeyDown(GLFW_KEY_D))
    {
        movement += right;
    }
    if (input.IsKeyDown(GLFW_KEY_A))
    {
        movement -= right;
    }
    if (input.IsKeyDown(GLFW_KEY_E))
    {
        movement += glm::vec3{0.0F, 1.0F, 0.0F};
    }
    if (input.IsKeyDown(GLFW_KEY_Q))
    {
        movement -= glm::vec3{0.0F, 1.0F, 0.0F};
    }

    if (glm::length(movement) > 1.0e-5F)
    {
        movement = glm::normalize(movement);
    }

    float speed = m_cameraSpeed;
    if (input.IsKeyDown(GLFW_KEY_LEFT_SHIFT))
    {
        speed *= 2.2F;
    }
    m_cameraPosition += movement * speed * deltaSeconds;
}

void LevelEditor::HandleEditorHotkeys(const engine::platform::Input& input, bool controlsEnabled)
{
    if (!controlsEnabled)
    {
        return;
    }

#if BUILD_WITH_IMGUI
    if (ImGui::GetIO().WantCaptureKeyboard || ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }
#endif

    if (input.IsKeyPressed(GLFW_KEY_1))
    {
        m_gizmoMode = GizmoMode::Translate;
    }
    if (input.IsKeyPressed(GLFW_KEY_2))
    {
        m_gizmoMode = GizmoMode::Rotate;
    }
    if (input.IsKeyPressed(GLFW_KEY_3))
    {
        m_gizmoMode = GizmoMode::Scale;
    }
    if (input.IsKeyPressed(GLFW_KEY_T))
    {
        m_topDownView = !m_topDownView;
    }
    if (input.IsKeyPressed(GLFW_KEY_G))
    {
        m_gridSnap = !m_gridSnap;
    }
    if (input.IsKeyPressed(GLFW_KEY_F2))
    {
        m_debugView = !m_debugView;
    }
    if (input.IsKeyPressed(GLFW_KEY_F3))
    {
        m_pendingRenderMode =
            (m_currentRenderMode == engine::render::RenderMode::Wireframe)
                ? engine::render::RenderMode::Filled
                : engine::render::RenderMode::Wireframe;
    }
    if (input.IsKeyPressed(GLFW_KEY_R) && m_mode == Mode::MapEditor)
    {
        m_pendingPlacementRotation = (m_pendingPlacementRotation + 90) % 360;
    }
    if (input.IsKeyPressed(GLFW_KEY_P) && m_mode == Mode::MapEditor)
    {
        m_propPlacementMode = !m_propPlacementMode;
        if (m_propPlacementMode)
        {
            m_lightPlacementMode = false;
        }
    }
    if (input.IsKeyPressed(GLFW_KEY_L) && m_mode == Mode::MapEditor)
    {
        m_lightPlacementMode = !m_lightPlacementMode;
        if (m_lightPlacementMode)
        {
            m_propPlacementMode = false;
        }
    }

    const bool ctrlDown = input.IsKeyDown(GLFW_KEY_LEFT_CONTROL) || input.IsKeyDown(GLFW_KEY_RIGHT_CONTROL);
    const bool shiftDown = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);

    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_Z))
    {
        if (shiftDown)
        {
            Redo();
        }
        else
        {
            Undo();
        }
        return;
    }
    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_Y))
    {
        Redo();
        return;
    }
    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_C))
    {
        CopyCurrentSelection();
        return;
    }
    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_V))
    {
        PasteClipboard();
        return;
    }

    if (input.IsKeyPressed(GLFW_KEY_DELETE))
    {
        DeleteCurrentSelection();
    }

    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_D))
    {
        DuplicateCurrentSelection();
    }
}

void LevelEditor::UpdateHoveredTile(const engine::platform::Input& input, int framebufferWidth, int framebufferHeight)
{
    m_hoveredTileValid = false;
    glm::vec3 rayOrigin{0.0F};
    glm::vec3 rayDirection{0.0F};
    if (!BuildMouseRay(input, framebufferWidth, framebufferHeight, &rayOrigin, &rayDirection))
    {
        return;
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectGround(rayOrigin, rayDirection, 0.0F, &hit))
    {
        return;
    }
    m_hoveredWorld = hit;

    const float halfWidth = static_cast<float>(m_map.width) * m_map.tileSize * 0.5F;
    const float halfHeight = static_cast<float>(m_map.height) * m_map.tileSize * 0.5F;
    const int tileX = static_cast<int>(std::floor((hit.x + halfWidth) / m_map.tileSize));
    const int tileY = static_cast<int>(std::floor((hit.z + halfHeight) / m_map.tileSize));

    if (tileX < 0 || tileY < 0 || tileX >= m_map.width || tileY >= m_map.height)
    {
        return;
    }

    m_hoveredTile = glm::ivec2{tileX, tileY};
    m_hoveredTileValid = true;
}

bool LevelEditor::BuildMouseRay(
    const engine::platform::Input& input,
    int framebufferWidth,
    int framebufferHeight,
    glm::vec3* outOrigin,
    glm::vec3* outDirection
) const
{
    if (framebufferWidth <= 0 || framebufferHeight <= 0 || outOrigin == nullptr || outDirection == nullptr)
    {
        return false;
    }

    const glm::vec2 mouse = input.MousePosition();
    const float x = (2.0F * mouse.x) / static_cast<float>(framebufferWidth) - 1.0F;
    const float y = 1.0F - (2.0F * mouse.y) / static_cast<float>(framebufferHeight);

    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), aspect, 0.05F, 900.0F);
    const glm::vec3 forward = CameraForward();
    const glm::mat4 view = glm::lookAt(m_cameraPosition, m_cameraPosition + forward, CameraUp());
    const glm::mat4 inv = glm::inverse(projection * view);

    const glm::vec4 nearClip = inv * glm::vec4{x, y, -1.0F, 1.0F};
    const glm::vec4 farClip = inv * glm::vec4{x, y, 1.0F, 1.0F};
    if (std::abs(nearClip.w) < 1.0e-6F || std::abs(farClip.w) < 1.0e-6F)
    {
        return false;
    }

    const glm::vec3 nearWorld = glm::vec3(nearClip) / nearClip.w;
    const glm::vec3 farWorld = glm::vec3(farClip) / farClip.w;
    const glm::vec3 direction = farWorld - nearWorld;
    if (glm::length(direction) < 1.0e-6F)
    {
        return false;
    }

    *outOrigin = nearWorld;
    *outDirection = glm::normalize(direction);
    return true;
}

bool LevelEditor::RayIntersectGround(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    float groundY,
    glm::vec3* outHit
) const
{
    if (std::abs(rayDirection.y) < 1.0e-6F)
    {
        return false;
    }

    const float t = (groundY - rayOrigin.y) / rayDirection.y;
    if (t < 0.0F)
    {
        return false;
    }

    if (outHit != nullptr)
    {
        *outHit = rayOrigin + rayDirection * t;
    }
    return true;
}

LevelEditor::Selection LevelEditor::PickSelection(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const
{
    float bestT = 1.0e9F;
    Selection best{};

    if (m_mode == Mode::LoopEditor)
    {
        for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
        {
            const LoopElement& element = m_loop.elements[static_cast<std::size_t>(i)];
            const glm::vec3 pickExtents = RotateExtentsXYZ(element.halfExtents, ElementRotation(element));
            const glm::vec3 minBounds = element.position - pickExtents;
            const glm::vec3 maxBounds = element.position + pickExtents;
            float t = 0.0F;
            if (!SegmentIntersectsAabb(rayOrigin, rayDirection, minBounds, maxBounds, &t))
            {
                continue;
            }
            if (t < bestT)
            {
                bestT = t;
                best.kind = SelectionKind::LoopElement;
                best.index = i;
            }
        }
        return best;
    }

    for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
    {
        const PropInstance& prop = m_map.props[static_cast<std::size_t>(i)];
        const glm::vec3 extents = RotateExtentsXYZ(prop.halfExtents, PropRotation(prop));
        const glm::vec3 minBounds = prop.position - extents;
        const glm::vec3 maxBounds = prop.position + extents;
        float t = 0.0F;
        if (!SegmentIntersectsAabb(rayOrigin, rayDirection, minBounds, maxBounds, &t))
        {
            continue;
        }
        if (t < bestT)
        {
            bestT = t;
            best.kind = SelectionKind::Prop;
            best.index = i;
        }
    }

    for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
    {
        const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset loop;
        std::string error;
        if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
        {
            continue;
        }
        const glm::ivec2 footprint = RotatedFootprintFor(loop, placement.rotationDegrees);
        const glm::vec3 center = TileCenter(placement.tileX, placement.tileY) +
                                 glm::vec3{
                                     (static_cast<float>(footprint.x) - 1.0F) * m_map.tileSize * 0.5F,
                                     1.0F,
                                     (static_cast<float>(footprint.y) - 1.0F) * m_map.tileSize * 0.5F,
                                 };
        const glm::vec3 extents{
            static_cast<float>(footprint.x) * m_map.tileSize * 0.5F,
            2.0F,
            static_cast<float>(footprint.y) * m_map.tileSize * 0.5F,
        };

        float t = 0.0F;
        if (!SegmentIntersectsAabb(rayOrigin, rayDirection, center - extents, center + extents, &t))
        {
            continue;
        }
        if (t < bestT)
        {
            bestT = t;
            best.kind = SelectionKind::MapPlacement;
            best.index = i;
        }
    }

    return best;
}

glm::vec3 LevelEditor::SelectionPivot() const
{
    if (m_selection.kind == SelectionKind::LoopElement)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
        if (indices.empty())
        {
            return glm::vec3{0.0F};
        }

        glm::vec3 pivot{0.0F};
        for (int idx : indices)
        {
            pivot += m_loop.elements[static_cast<std::size_t>(idx)].position;
        }
        return pivot / static_cast<float>(indices.size());
    }

    if (m_selection.kind == SelectionKind::MapPlacement)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
        if (indices.empty())
        {
            return glm::vec3{0.0F};
        }

        glm::vec3 pivot{0.0F};
        int validCount = 0;
        for (int idx : indices)
        {
            if (idx < 0 || idx >= static_cast<int>(m_map.placements.size()))
            {
                continue;
            }
            const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
            LoopAsset loop;
            std::string error;
            if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
            {
                continue;
            }
            const glm::ivec2 footprint = RotatedFootprint(loop, placement.rotationDegrees);
            pivot += TileCenter(placement.tileX, placement.tileY) +
                     glm::vec3{
                         (static_cast<float>(footprint.x) - 1.0F) * m_map.tileSize * 0.5F,
                         0.0F,
                         (static_cast<float>(footprint.y) - 1.0F) * m_map.tileSize * 0.5F,
                     };
            ++validCount;
        }
        if (validCount == 0)
        {
            return glm::vec3{0.0F};
        }
        return pivot / static_cast<float>(validCount);
    }

    if (m_selection.kind == SelectionKind::Prop)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
        if (indices.empty())
        {
            return glm::vec3{0.0F};
        }

        glm::vec3 pivot{0.0F};
        for (int idx : indices)
        {
            pivot += m_map.props[static_cast<std::size_t>(idx)].position;
        }
        return pivot / static_cast<float>(indices.size());
    }

    return glm::vec3{0.0F};
}

bool LevelEditor::RayIntersectPlane(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal,
    glm::vec3* outHit
) const
{
    const float denom = glm::dot(rayDirection, planeNormal);
    if (std::abs(denom) < 1.0e-6F)
    {
        return false;
    }

    const float t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
    if (t < 0.0F)
    {
        return false;
    }

    if (outHit != nullptr)
    {
        *outHit = rayOrigin + rayDirection * t;
    }
    return true;
}

bool LevelEditor::StartAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (m_selection.kind == SelectionKind::None)
    {
        return false;
    }
    if (m_selection.kind == SelectionKind::MapPlacement && m_gizmoMode != GizmoMode::Translate)
    {
        return false;
    }

    auto hasUnlocked = [&]() {
        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                if (!m_loop.elements[static_cast<std::size_t>(idx)].transformLocked)
                {
                    return true;
                }
            }
            return false;
        }
        if (m_selection.kind == SelectionKind::MapPlacement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
            for (int idx : indices)
            {
                if (!m_map.placements[static_cast<std::size_t>(idx)].transformLocked)
                {
                    return true;
                }
            }
            return false;
        }
        if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                if (!m_map.props[static_cast<std::size_t>(idx)].transformLocked)
                {
                    return true;
                }
            }
            return false;
        }
        return false;
    };

    if (!hasUnlocked())
    {
        m_statusLine = "Selection is transform-locked";
        return false;
    }

    const glm::vec3 pivot = SelectionPivot();
    const float cameraDistance = glm::length(m_cameraPosition - pivot);
    const float axisLength = glm::clamp(cameraDistance * 0.18F, 1.8F, 10.0F);
    const float handleHalf = glm::max(0.3F, axisLength * 0.14F);
    const glm::vec3 axisDirections[3] = {
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{0.0F, 1.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 1.0F},
    };

    float bestT = 1.0e9F;
    GizmoAxis bestAxis = GizmoAxis::None;
    glm::vec3 bestDirection{0.0F};
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
    {
        if (m_selection.kind == SelectionKind::MapPlacement && axisIndex == 1)
        {
            continue;
        }
        const glm::vec3 direction = axisDirections[axisIndex];
        const glm::vec3 tip = pivot + direction * axisLength;
        const glm::vec3 minBounds = tip - glm::vec3{handleHalf};
        const glm::vec3 maxBounds = tip + glm::vec3{handleHalf};
        float t = 0.0F;
        if (!SegmentIntersectsAabb(rayOrigin, rayDirection, minBounds, maxBounds, &t))
        {
            continue;
        }
        if (t < bestT)
        {
            bestT = t;
            bestDirection = direction;
            bestAxis = axisIndex == 0 ? GizmoAxis::X : (axisIndex == 1 ? GizmoAxis::Y : GizmoAxis::Z);
        }
    }

    if (bestAxis == GizmoAxis::None)
    {
        return false;
    }

    glm::vec3 planeNormal{0.0F};
    if (m_gizmoMode == GizmoMode::Rotate)
    {
        planeNormal = bestDirection;
    }
    else
    {
        const glm::vec3 forward = CameraForward();
        planeNormal = glm::cross(bestDirection, forward);
        if (glm::length(planeNormal) < 1.0e-4F)
        {
            planeNormal = glm::cross(bestDirection, glm::vec3{0.0F, 1.0F, 0.0F});
        }
        if (glm::length(planeNormal) < 1.0e-4F)
        {
            planeNormal = glm::cross(bestDirection, glm::vec3{1.0F, 0.0F, 0.0F});
        }
        if (glm::length(planeNormal) < 1.0e-4F)
        {
            return false;
        }
        planeNormal = glm::normalize(planeNormal);
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, pivot, planeNormal, &hit))
    {
        return false;
    }

    m_axisDragActive = true;
    m_axisDragAxis = bestAxis;
    m_axisDragPivot = pivot;
    m_axisDragDirection = bestDirection;
    m_axisDragPlaneNormal = planeNormal;
    m_axisDragMode = m_gizmoMode;
    if (m_axisDragMode == GizmoMode::Rotate)
    {
        glm::vec3 startVector = hit - pivot;
        startVector -= bestDirection * glm::dot(startVector, bestDirection);
        if (glm::length(startVector) < 1.0e-4F)
        {
            return false;
        }
        startVector = glm::normalize(startVector);
        m_axisDragLastVector = startVector;
        m_axisDragStartScalar = 0.0F;
        m_axisDragLastScalar = 0.0F;
    }
    else
    {
        m_axisDragStartScalar = glm::dot(hit - pivot, bestDirection);
        m_axisDragLastScalar = m_axisDragStartScalar;
        m_axisDragLastVector = glm::vec3{1.0F, 0.0F, 0.0F};
    }
    PushHistorySnapshot();
    m_gizmoEditing = true;
    const char* axisText = bestAxis == GizmoAxis::X ? "X" : (bestAxis == GizmoAxis::Y ? "Y" : "Z");
    m_statusLine = std::string{"Gizmo drag: "} + GizmoToText(m_gizmoMode) + " axis " + axisText;
    return true;
}

void LevelEditor::UpdateAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (!m_axisDragActive || m_axisDragAxis == GizmoAxis::None)
    {
        return;
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, m_axisDragPivot, m_axisDragPlaneNormal, &hit))
    {
        return;
    }

    const float scalar = glm::dot(hit - m_axisDragPivot, m_axisDragDirection);
    const float previousScalar = m_axisDragLastScalar;

    float delta = 0.0F;
    if (m_axisDragMode == GizmoMode::Rotate)
    {
        glm::vec3 currentVector = hit - m_axisDragPivot;
        currentVector -= m_axisDragDirection * glm::dot(currentVector, m_axisDragDirection);
        if (glm::length(currentVector) < 1.0e-4F)
        {
            return;
        }
        currentVector = glm::normalize(currentVector);
        const glm::vec3 previousVector = m_axisDragLastVector;
        const float sinTerm = glm::dot(m_axisDragDirection, glm::cross(previousVector, currentVector));
        const float cosTerm = glm::dot(previousVector, currentVector);
        const float deltaDegreesRaw = glm::degrees(std::atan2(sinTerm, cosTerm));
        float appliedDegrees = deltaDegreesRaw;
        if (m_angleSnap)
        {
            const float stepDegrees = std::max(1.0F, m_angleStepDegrees);
            const float accumulatedNow = m_axisDragLastScalar + deltaDegreesRaw;
            const float snappedNow = std::round(accumulatedNow / stepDegrees) * stepDegrees;
            const float snappedBefore = std::round(m_axisDragLastScalar / stepDegrees) * stepDegrees;
            appliedDegrees = snappedNow - snappedBefore;
            m_axisDragLastScalar = accumulatedNow;
        }
        else
        {
            m_axisDragLastScalar += deltaDegreesRaw;
        }
        m_axisDragLastVector = currentVector;
        if (std::abs(appliedDegrees) < 1.0e-6F)
        {
            return;
        }

        auto applyRotationDelta = [&](LoopElement& element) {
            if (element.transformLocked)
            {
                return;
            }
            if (m_axisDragAxis == GizmoAxis::X)
            {
                element.pitchDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Y)
            {
                element.yawDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Z)
            {
                element.rollDegrees += appliedDegrees;
            }
        };
        auto applyRotationDeltaProp = [&](PropInstance& prop) {
            if (prop.transformLocked)
            {
                return;
            }
            if (m_axisDragAxis == GizmoAxis::X)
            {
                prop.pitchDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Y)
            {
                prop.yawDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Z)
            {
                prop.rollDegrees += appliedDegrees;
            }
        };

        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                applyRotationDelta(m_loop.elements[static_cast<std::size_t>(idx)]);
            }
            AutoComputeLoopBoundsAndFootprint();
        }
        else if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                applyRotationDeltaProp(m_map.props[static_cast<std::size_t>(idx)]);
            }
        }
        return;
    }

    if (m_axisDragMode == GizmoMode::Translate)
    {
        if (m_selection.kind == SelectionKind::MapPlacement)
        {
            const int tileDelta = static_cast<int>(std::round((scalar - previousScalar) / m_map.tileSize));
            if (tileDelta == 0)
            {
                m_axisDragLastScalar = scalar;
                return;
            }
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
            for (int idx : indices)
            {
                LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
                if (placement.transformLocked)
                {
                    continue;
                }
                const int nextX = placement.tileX + (m_axisDragAxis == GizmoAxis::X ? tileDelta : 0);
                const int nextY = placement.tileY + (m_axisDragAxis == GizmoAxis::Z ? tileDelta : 0);
                if (CanPlaceLoopAt(nextX, nextY, placement.rotationDegrees, idx))
                {
                    placement.tileX = nextX;
                    placement.tileY = nextY;
                }
            }
            m_axisDragLastScalar = scalar;
            return;
        }

        delta = scalar - previousScalar;
        if (m_gridSnap)
        {
            const float step = std::max(0.1F, m_gridStep);
            const float snappedNow = std::round((scalar - m_axisDragStartScalar) / step) * step;
            const float snappedBefore = std::round((previousScalar - m_axisDragStartScalar) / step) * step;
            delta = snappedNow - snappedBefore;
        }
        if (std::abs(delta) < 1.0e-6F)
        {
            m_axisDragLastScalar = scalar;
            return;
        }

        const glm::vec3 move = m_axisDragDirection * delta;
        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                LoopElement& element = m_loop.elements[static_cast<std::size_t>(idx)];
                if (!element.transformLocked)
                {
                    element.position += move;
                }
            }
            AutoComputeLoopBoundsAndFootprint();
        }
        else if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                PropInstance& prop = m_map.props[static_cast<std::size_t>(idx)];
                if (!prop.transformLocked)
                {
                    prop.position += move;
                }
            }
        }
        m_axisDragLastScalar = scalar;
        return;
    }

    if (m_axisDragMode == GizmoMode::Scale)
    {
        const int axisComponent = m_axisDragAxis == GizmoAxis::X ? 0 : (m_axisDragAxis == GizmoAxis::Y ? 1 : 2);
        delta = scalar - previousScalar;

        if (m_gridSnap)
        {
            const float step = std::max(0.1F, m_gridStep);
            const float snappedNow = std::round((scalar - m_axisDragStartScalar) / step) * step;
            const float snappedBefore = std::round((previousScalar - m_axisDragStartScalar) / step) * step;
            delta = snappedNow - snappedBefore;
        }
        if (std::abs(delta) < 1.0e-6F)
        {
            m_axisDragLastScalar = scalar;
            return;
        }

        const float scaleDelta = delta * 0.35F;
        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                LoopElement& element = m_loop.elements[static_cast<std::size_t>(idx)];
                if (!element.transformLocked)
                {
                    element.halfExtents[axisComponent] = std::max(0.05F, element.halfExtents[axisComponent] + scaleDelta);
                }
            }
            AutoComputeLoopBoundsAndFootprint();
        }
        else if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                PropInstance& prop = m_map.props[static_cast<std::size_t>(idx)];
                if (!prop.transformLocked)
                {
                    prop.halfExtents[axisComponent] = std::max(0.05F, prop.halfExtents[axisComponent] + scaleDelta);
                }
            }
        }
        m_axisDragLastScalar = scalar;
    }
}

void LevelEditor::StopAxisDrag()
{
    m_axisDragActive = false;
    m_axisDragAxis = GizmoAxis::None;
    m_axisDragMode = GizmoMode::Translate;
    m_axisDragDirection = glm::vec3{1.0F, 0.0F, 0.0F};
    m_axisDragPlaneNormal = glm::vec3{0.0F, 1.0F, 0.0F};
    m_axisDragLastVector = glm::vec3{1.0F, 0.0F, 0.0F};
    m_gizmoEditing = false;
}

void LevelEditor::ApplyGizmoInput(const engine::platform::Input& input, float deltaSeconds)
{
    if (m_selection.kind == SelectionKind::None)
    {
        m_gizmoEditing = false;
        return;
    }

#if BUILD_WITH_IMGUI
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        m_gizmoEditing = false;
        return;
    }
#endif

    const float moveStep = m_gridSnap ? std::max(0.1F, m_gridStep) : std::max(0.05F, 4.0F * deltaSeconds);
    const float angleStep = m_angleSnap ? std::max(1.0F, m_angleStepDegrees) : (75.0F * deltaSeconds);

    if (m_selection.kind == SelectionKind::LoopElement)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
        if (indices.empty())
        {
            m_gizmoEditing = false;
            return;
        }

        const bool translateHeld = input.IsKeyDown(GLFW_KEY_LEFT) ||
                                   input.IsKeyDown(GLFW_KEY_RIGHT) ||
                                   input.IsKeyDown(GLFW_KEY_UP) ||
                                   input.IsKeyDown(GLFW_KEY_DOWN) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_UP) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_DOWN);
        const bool rotateHeld = input.IsKeyDown(GLFW_KEY_LEFT_BRACKET) ||
                                input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET);
        const bool scaleHeld = input.IsKeyDown(GLFW_KEY_EQUAL) ||
                               input.IsKeyDown(GLFW_KEY_MINUS);
        const bool activeEdit =
            (m_gizmoMode == GizmoMode::Translate && translateHeld) ||
            (m_gizmoMode == GizmoMode::Rotate && rotateHeld) ||
            (m_gizmoMode == GizmoMode::Scale && scaleHeld);

        if (!activeEdit)
        {
            m_gizmoEditing = false;
            return;
        }
        if (!m_gizmoEditing)
        {
            PushHistorySnapshot();
            m_gizmoEditing = true;
        }

        for (int idx : indices)
        {
            LoopElement& element = m_loop.elements[static_cast<std::size_t>(idx)];
            if (element.transformLocked)
            {
                continue;
            }
            if (m_gizmoMode == GizmoMode::Translate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT))
                {
                    element.position.x -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT))
                {
                    element.position.x += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_UP))
                {
                    element.position.z -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_DOWN))
                {
                    element.position.z += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_UP))
                {
                    element.position.y += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_DOWN))
                {
                    element.position.y -= moveStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Rotate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT_BRACKET))
                {
                    element.yawDegrees -= angleStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET))
                {
                    element.yawDegrees += angleStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Scale)
            {
                if (input.IsKeyDown(GLFW_KEY_EQUAL))
                {
                    element.halfExtents += glm::vec3{moveStep * 0.5F};
                }
                if (input.IsKeyDown(GLFW_KEY_MINUS))
                {
                    element.halfExtents -= glm::vec3{moveStep * 0.5F};
                    element.halfExtents = glm::max(element.halfExtents, glm::vec3{0.05F});
                }
            }
        }
        AutoComputeLoopBoundsAndFootprint();
        return;
    }

    if (m_selection.kind == SelectionKind::MapPlacement)
    {
        m_gizmoEditing = false;
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
        if (indices.empty())
        {
            return;
        }

        if (m_gizmoMode == GizmoMode::Translate)
        {
            int dx = 0;
            int dy = 0;
            if (input.IsKeyPressed(GLFW_KEY_LEFT))
            {
                dx -= 1;
            }
            if (input.IsKeyPressed(GLFW_KEY_RIGHT))
            {
                dx += 1;
            }
            if (input.IsKeyPressed(GLFW_KEY_UP))
            {
                dy -= 1;
            }
            if (input.IsKeyPressed(GLFW_KEY_DOWN))
            {
                dy += 1;
            }

            if (dx != 0 || dy != 0)
            {
                PushHistorySnapshot();
                for (int idx : indices)
                {
                    LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
                    const int newX = placement.tileX + dx;
                    const int newY = placement.tileY + dy;
                if (CanPlaceLoopAt(newX, newY, placement.rotationDegrees, idx))
                {
                    if (!placement.transformLocked)
                    {
                        placement.tileX = newX;
                        placement.tileY = newY;
                    }
                }
            }
            }
        }
        else if (m_gizmoMode == GizmoMode::Rotate)
        {
            int rotationDelta = 0;
            if (input.IsKeyPressed(GLFW_KEY_LEFT_BRACKET))
            {
                rotationDelta -= 90;
            }
            if (input.IsKeyPressed(GLFW_KEY_RIGHT_BRACKET))
            {
                rotationDelta += 90;
            }
            if (rotationDelta != 0)
            {
                PushHistorySnapshot();
                for (int idx : indices)
                {
                    LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
                    int nextRot = ((placement.rotationDegrees + rotationDelta) % 360 + 360) % 360;
                    if (!placement.transformLocked && CanPlaceLoopAt(placement.tileX, placement.tileY, nextRot, idx))
                    {
                        placement.rotationDegrees = nextRot;
                    }
                }
            }
        }
        return;
    }

    if (m_selection.kind == SelectionKind::Prop)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
        if (indices.empty())
        {
            m_gizmoEditing = false;
            return;
        }

        const bool translateHeld = input.IsKeyDown(GLFW_KEY_LEFT) ||
                                   input.IsKeyDown(GLFW_KEY_RIGHT) ||
                                   input.IsKeyDown(GLFW_KEY_UP) ||
                                   input.IsKeyDown(GLFW_KEY_DOWN) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_UP) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_DOWN);
        const bool rotateHeld = input.IsKeyDown(GLFW_KEY_LEFT_BRACKET) ||
                                input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET);
        const bool scaleHeld = input.IsKeyDown(GLFW_KEY_EQUAL) ||
                               input.IsKeyDown(GLFW_KEY_MINUS);
        const bool activeEdit =
            (m_gizmoMode == GizmoMode::Translate && translateHeld) ||
            (m_gizmoMode == GizmoMode::Rotate && rotateHeld) ||
            (m_gizmoMode == GizmoMode::Scale && scaleHeld);

        if (!activeEdit)
        {
            m_gizmoEditing = false;
            return;
        }
        if (!m_gizmoEditing)
        {
            PushHistorySnapshot();
            m_gizmoEditing = true;
        }

        for (int idx : indices)
        {
            PropInstance& prop = m_map.props[static_cast<std::size_t>(idx)];
            if (prop.transformLocked)
            {
                continue;
            }
            if (m_gizmoMode == GizmoMode::Translate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT))
                {
                    prop.position.x -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT))
                {
                    prop.position.x += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_UP))
                {
                    prop.position.z -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_DOWN))
                {
                    prop.position.z += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_UP))
                {
                    prop.position.y += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_DOWN))
                {
                    prop.position.y -= moveStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Rotate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT_BRACKET))
                {
                    prop.yawDegrees -= angleStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET))
                {
                    prop.yawDegrees += angleStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Scale)
            {
                if (input.IsKeyDown(GLFW_KEY_EQUAL))
                {
                    prop.halfExtents += glm::vec3{moveStep * 0.35F};
                }
                if (input.IsKeyDown(GLFW_KEY_MINUS))
                {
                    prop.halfExtents -= glm::vec3{moveStep * 0.35F};
                    prop.halfExtents = glm::max(prop.halfExtents, glm::vec3{0.05F});
                }
            }
        }
    }
}

glm::ivec2 LevelEditor::RotatedFootprint(const LoopAsset& loop, int rotationDegrees) const
{
    return RotatedFootprintFor(loop, rotationDegrees);
}

bool LevelEditor::CanPlaceLoopAt(int tileX, int tileY, int rotationDegrees, int ignoredPlacement) const
{
    if (m_paletteLoopIndex < 0 || m_paletteLoopIndex >= static_cast<int>(m_loopLibrary.size()))
    {
        return false;
    }

    LoopAsset selectedLoop;
    std::string error;
    if (!LevelAssetIO::LoadLoop(m_loopLibrary[static_cast<std::size_t>(m_paletteLoopIndex)], &selectedLoop, &error))
    {
        return false;
    }
    const glm::ivec2 newFootprint = RotatedFootprint(selectedLoop, rotationDegrees);
    if (tileX < 0 || tileY < 0 || tileX + newFootprint.x > m_map.width || tileY + newFootprint.y > m_map.height)
    {
        return false;
    }

    auto overlapRect = [](int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
        return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
    };

    for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
    {
        if (i == ignoredPlacement)
        {
            continue;
        }

        const LoopPlacement& existing = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset existingLoop;
        if (!LevelAssetIO::LoadLoop(existing.loopId, &existingLoop, &error))
        {
            continue;
        }
        const glm::ivec2 existingFootprint = RotatedFootprint(existingLoop, existing.rotationDegrees);
        if (overlapRect(
                tileX,
                tileY,
                newFootprint.x,
                newFootprint.y,
                existing.tileX,
                existing.tileY,
                existingFootprint.x,
                existingFootprint.y))
        {
            return false;
        }
    }
    return true;
}

glm::vec3 LevelEditor::TileCenter(int tileX, int tileY) const
{
    const float halfWidth = static_cast<float>(m_map.width) * m_map.tileSize * 0.5F;
    const float halfHeight = static_cast<float>(m_map.height) * m_map.tileSize * 0.5F;
    return glm::vec3{
        -halfWidth + m_map.tileSize * 0.5F + static_cast<float>(tileX) * m_map.tileSize,
        0.0F,
        -halfHeight + m_map.tileSize * 0.5F + static_cast<float>(tileY) * m_map.tileSize,
    };
}

void LevelEditor::PlaceLoopAtHoveredTile()
{
    if (!m_hoveredTileValid || m_paletteLoopIndex < 0 || m_paletteLoopIndex >= static_cast<int>(m_loopLibrary.size()))
    {
        return;
    }

    if (!CanPlaceLoopAt(m_hoveredTile.x, m_hoveredTile.y, m_pendingPlacementRotation, -1))
    {
        m_statusLine = "Placement invalid (overlap or out of bounds)";
        return;
    }

    LoopPlacement placement;
    placement.loopId = m_loopLibrary[static_cast<std::size_t>(m_paletteLoopIndex)];
    placement.tileX = m_hoveredTile.x;
    placement.tileY = m_hoveredTile.y;
    placement.rotationDegrees = m_pendingPlacementRotation;
    PushHistorySnapshot();
    m_map.placements.push_back(placement);
    SelectSingle(Selection{SelectionKind::MapPlacement, static_cast<int>(m_map.placements.size()) - 1});
    m_statusLine = "Placed loop " + placement.loopId;
}

bool LevelEditor::EnsureQuickLoopAsset(LoopElementType type, std::string* outLoopId)
{
    LoopAsset quickLoop;
    quickLoop.id = QuickLoopAssetId(type);
    quickLoop.displayName = std::string{"Quick "} + LoopElementTypeToText(type);
    quickLoop.manualBounds = true;
    quickLoop.manualFootprint = true;
    quickLoop.footprintWidth = 1;
    quickLoop.footprintHeight = 1;
    quickLoop.boundsMin = glm::vec3{-8.0F, 0.0F, -8.0F};
    quickLoop.boundsMax = glm::vec3{8.0F, 2.5F, 8.0F};

    LoopElement element;
    element.type = type;
    element.name = std::string{LoopElementTypeToText(type)} + "_1";
    element.position = glm::vec3{0.0F, type == LoopElementType::Marker ? 0.35F : 1.0F, 0.0F};
    element.halfExtents = QuickLoopDefaultHalfExtents(type);
    if (type == LoopElementType::Pallet)
    {
        element.position.y = 0.85F;
    }
    if (type == LoopElementType::Marker)
    {
        element.markerTag = "generic_marker";
    }
    quickLoop.elements.push_back(element);

    std::string error;
    if (!LevelAssetIO::SaveLoop(quickLoop, &error))
    {
        m_statusLine = "Quick loop save failed: " + error;
        return false;
    }

    RefreshLibraries();
    if (outLoopId != nullptr)
    {
        *outLoopId = quickLoop.id;
    }
    return true;
}

void LevelEditor::PlaceQuickLoopObjectAtHovered(LoopElementType type)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Quick loop placement requires Map Editor + hovered tile";
        return;
    }

    std::string loopId;
    if (!EnsureQuickLoopAsset(type, &loopId))
    {
        return;
    }

    const auto it = std::find(m_loopLibrary.begin(), m_loopLibrary.end(), loopId);
    if (it == m_loopLibrary.end())
    {
        m_statusLine = "Quick loop asset not found in library";
        return;
    }

    m_paletteLoopIndex = static_cast<int>(std::distance(m_loopLibrary.begin(), it));
    PlaceLoopAtHoveredTile();
    if (m_statusLine.rfind("Placed loop ", 0) == 0)
    {
        m_statusLine = std::string{"Placed quick "} + LoopElementTypeToText(type);
    }
}

void LevelEditor::RemovePlacementAtHoveredTile()
{
    if (!m_hoveredTileValid)
    {
        return;
    }

    for (int i = static_cast<int>(m_map.placements.size()) - 1; i >= 0; --i)
    {
        const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset loop;
        std::string error;
        if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
        {
            continue;
        }

        const glm::ivec2 footprint = RotatedFootprint(loop, placement.rotationDegrees);
        if (m_hoveredTile.x >= placement.tileX &&
            m_hoveredTile.x < placement.tileX + footprint.x &&
            m_hoveredTile.y >= placement.tileY &&
            m_hoveredTile.y < placement.tileY + footprint.y)
        {
            PushHistorySnapshot();
            m_map.placements.erase(m_map.placements.begin() + i);
            m_statusLine = "Removed loop placement";
            ClearSelections();
            return;
        }
    }
}

void LevelEditor::AddPropAtHoveredTile()
{
    if (!m_hoveredTileValid)
    {
        return;
    }

    PushHistorySnapshot();
    PropInstance prop;
    prop.name = BuildUniquePropName("prop");
    prop.type = m_selectedPropType;
    prop.position = glm::vec3{m_hoveredWorld.x, 0.85F, m_hoveredWorld.z};
    switch (prop.type)
    {
        case PropType::Rock: prop.halfExtents = glm::vec3{0.9F, 0.9F, 0.9F}; break;
        case PropType::Tree: prop.halfExtents = glm::vec3{0.6F, 1.6F, 0.6F}; break;
        case PropType::Obstacle: prop.halfExtents = glm::vec3{1.2F, 1.0F, 0.7F}; break;
        case PropType::Platform:
            prop.halfExtents = glm::vec3{2.2F, 0.25F, 2.2F};
            prop.position.y = 0.55F;
            break;
        case PropType::MeshAsset: prop.halfExtents = glm::vec3{0.8F, 0.8F, 0.8F}; break;
        default: break;
    }
    prop.colliderHalfExtents = prop.halfExtents;
    prop.colliderType = ColliderType::Box;
    m_map.props.push_back(prop);
    SelectSingle(Selection{SelectionKind::Prop, static_cast<int>(m_map.props.size()) - 1});
    m_statusLine = std::string{"Added prop "} + PropToText(prop.type);
}

void LevelEditor::AddLightAtHovered(LightType type)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Hover valid tile to place light";
        return;
    }

    PushHistorySnapshot();
    LightInstance light;
    light.type = type;
    light.name = std::string(type == LightType::Spot ? "spot_light_" : "point_light_") + std::to_string(static_cast<int>(m_map.lights.size()) + 1);
    light.position = TileCenter(m_hoveredTile.x, m_hoveredTile.y) + glm::vec3{0.0F, type == LightType::Spot ? 3.0F : 2.5F, 0.0F};
    if (type == LightType::Spot)
    {
        light.rotationEuler = glm::vec3{-45.0F, glm::degrees(m_cameraYaw), 0.0F};
        light.spotInnerAngle = 22.0F;
        light.spotOuterAngle = 36.0F;
    }

    m_map.lights.push_back(light);
    m_selectedLightIndex = static_cast<int>(m_map.lights.size()) - 1;
    m_statusLine = std::string{"Added "} + (type == LightType::Spot ? "spot" : "point") + " light";
}

void LevelEditor::DeleteCurrentSelection()
{
    if (m_selection.kind == SelectionKind::None)
    {
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(m_selection.kind);
    if (indices.empty())
    {
        return;
    }

    PushHistorySnapshot();
    if (m_selection.kind == SelectionKind::LoopElement)
    {
        for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        {
            m_loop.elements.erase(m_loop.elements.begin() + *it);
        }
        AutoComputeLoopBoundsAndFootprint();
        m_statusLine = "Deleted loop element(s)";
    }
    else if (m_selection.kind == SelectionKind::MapPlacement)
    {
        for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        {
            m_map.placements.erase(m_map.placements.begin() + *it);
        }
        m_statusLine = "Deleted placement(s)";
    }
    else if (m_selection.kind == SelectionKind::Prop)
    {
        for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        {
            m_map.props.erase(m_map.props.begin() + *it);
        }
        m_statusLine = "Deleted prop(s)";
    }
    ClearSelections();
}

void LevelEditor::DuplicateCurrentSelection()
{
    if (m_selection.kind == SelectionKind::None)
    {
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(m_selection.kind);
    if (indices.empty())
    {
        return;
    }

    PushHistorySnapshot();

    if (m_selection.kind == SelectionKind::LoopElement)
    {
        std::vector<int> newIndices;
        for (int idx : indices)
        {
            LoopElement clone = m_loop.elements[static_cast<std::size_t>(idx)];
            clone.name = BuildUniqueLoopElementName(clone.name);
            clone.position += glm::vec3{m_gridSnap ? m_gridStep : 0.5F, 0.0F, m_gridSnap ? m_gridStep : 0.5F};
            m_loop.elements.push_back(clone);
            newIndices.push_back(static_cast<int>(m_loop.elements.size()) - 1);
        }
        AutoComputeLoopBoundsAndFootprint();
        ClearSelections();
        m_selectedLoopElements = newIndices;
        if (!newIndices.empty())
        {
            m_selection = Selection{SelectionKind::LoopElement, newIndices.back()};
        }
        m_statusLine = "Duplicated loop element(s)";
        return;
    }

    if (m_selection.kind == SelectionKind::MapPlacement)
    {
        std::vector<int> newIndices;
        for (int idx : indices)
        {
            LoopPlacement clone = m_map.placements[static_cast<std::size_t>(idx)];
            clone.tileX += 1;
            if (CanPlaceLoopAt(clone.tileX, clone.tileY, clone.rotationDegrees, -1))
            {
                m_map.placements.push_back(clone);
                newIndices.push_back(static_cast<int>(m_map.placements.size()) - 1);
            }
        }
        ClearSelections();
        m_selectedMapPlacements = newIndices;
        if (!newIndices.empty())
        {
            m_selection = Selection{SelectionKind::MapPlacement, newIndices.back()};
            m_statusLine = "Duplicated placement(s)";
        }
        else
        {
            m_statusLine = "Duplicate failed: no free space";
        }
        return;
    }

    if (m_selection.kind == SelectionKind::Prop)
    {
        std::vector<int> newIndices;
        for (int idx : indices)
        {
            PropInstance clone = m_map.props[static_cast<std::size_t>(idx)];
            clone.position += glm::vec3{m_gridSnap ? m_gridStep : 0.5F, 0.0F, m_gridSnap ? m_gridStep : 0.5F};
            m_map.props.push_back(clone);
            newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
        }
        ClearSelections();
        m_selectedProps = newIndices;
        if (!newIndices.empty())
        {
            m_selection = Selection{SelectionKind::Prop, newIndices.back()};
        }
        m_statusLine = "Duplicated prop(s)";
    }
}

void LevelEditor::CopyCurrentSelection()
{
    if (m_selection.kind == SelectionKind::None)
    {
        m_statusLine = "Copy: nothing selected";
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(m_selection.kind);
    if (indices.empty())
    {
        m_statusLine = "Copy: invalid selection";
        return;
    }

    m_clipboard = ClipboardState{};
    m_clipboard.kind = m_selection.kind;

    if (m_selection.kind == SelectionKind::LoopElement)
    {
        for (int idx : indices)
        {
            m_clipboard.loopElements.push_back(m_loop.elements[static_cast<std::size_t>(idx)]);
        }
        m_clipboard.hasData = !m_clipboard.loopElements.empty();
        m_statusLine = "Copied loop element(s): " + std::to_string(m_clipboard.loopElements.size());
    }
    else if (m_selection.kind == SelectionKind::MapPlacement)
    {
        for (int idx : indices)
        {
            m_clipboard.mapPlacements.push_back(m_map.placements[static_cast<std::size_t>(idx)]);
        }
        m_clipboard.hasData = !m_clipboard.mapPlacements.empty();
        m_statusLine = "Copied placement(s): " + std::to_string(m_clipboard.mapPlacements.size());
    }
    else if (m_selection.kind == SelectionKind::Prop)
    {
        for (int idx : indices)
        {
            m_clipboard.props.push_back(m_map.props[static_cast<std::size_t>(idx)]);
        }
        m_clipboard.hasData = !m_clipboard.props.empty();
        m_statusLine = "Copied prop(s): " + std::to_string(m_clipboard.props.size());
    }

    if (!m_clipboard.hasData)
    {
        m_statusLine = "Copy: unsupported selection";
        return;
    }
    m_clipboard.pasteCount = 0;
}

void LevelEditor::PasteClipboard()
{
    if (!m_clipboard.hasData || m_clipboard.kind == SelectionKind::None)
    {
        m_statusLine = "Paste: clipboard is empty";
        return;
    }

    if (m_clipboard.kind == SelectionKind::LoopElement && m_mode != Mode::LoopEditor)
    {
        m_statusLine = "Paste: loop elements only in Loop Editor";
        return;
    }
    if ((m_clipboard.kind == SelectionKind::MapPlacement || m_clipboard.kind == SelectionKind::Prop) &&
        m_mode != Mode::MapEditor)
    {
        m_statusLine = "Paste: map objects only in Map Editor";
        return;
    }

    const int pasteIndex = m_clipboard.pasteCount + 1;
    const float worldOffset = (m_gridSnap ? m_gridStep : 0.5F) * static_cast<float>(pasteIndex);
    const int tileOffset = pasteIndex;

    bool snapshotPushed = false;
    auto pushSnapshotOnce = [&]() {
        if (!snapshotPushed)
        {
            PushHistorySnapshot();
            snapshotPushed = true;
        }
    };

    if (m_clipboard.kind == SelectionKind::LoopElement)
    {
        std::vector<int> newIndices;
        for (const LoopElement& source : m_clipboard.loopElements)
        {
            pushSnapshotOnce();
            LoopElement clone = source;
            clone.name = BuildUniqueLoopElementName(source.name);
            clone.position += glm::vec3{worldOffset, 0.0F, worldOffset};
            m_loop.elements.push_back(clone);
            newIndices.push_back(static_cast<int>(m_loop.elements.size()) - 1);
        }

        if (newIndices.empty())
        {
            m_statusLine = "Paste failed";
            return;
        }
        AutoComputeLoopBoundsAndFootprint();
        ClearSelections();
        m_selectedLoopElements = newIndices;
        m_selection = Selection{SelectionKind::LoopElement, newIndices.back()};
        m_clipboard.pasteCount += 1;
        m_statusLine = "Pasted loop element(s): " + std::to_string(newIndices.size());
        return;
    }

    if (m_clipboard.kind == SelectionKind::MapPlacement)
    {
        std::vector<int> newIndices;
        for (const LoopPlacement& source : m_clipboard.mapPlacements)
        {
            LoopPlacement clone = source;
            clone.tileX += tileOffset;
            clone.tileY += tileOffset;
            if (!CanPlaceLoopAt(clone.tileX, clone.tileY, clone.rotationDegrees, -1))
            {
                continue;
            }
            pushSnapshotOnce();
            m_map.placements.push_back(clone);
            newIndices.push_back(static_cast<int>(m_map.placements.size()) - 1);
        }

        if (newIndices.empty())
        {
            m_statusLine = "Paste failed: no free map space";
            return;
        }
        ClearSelections();
        m_selectedMapPlacements = newIndices;
        m_selection = Selection{SelectionKind::MapPlacement, newIndices.back()};
        m_clipboard.pasteCount += 1;
        m_statusLine = "Pasted placement(s): " + std::to_string(newIndices.size());
        return;
    }

    if (m_clipboard.kind == SelectionKind::Prop)
    {
        std::vector<int> newIndices;
        for (const PropInstance& source : m_clipboard.props)
        {
            pushSnapshotOnce();
            PropInstance clone = source;
            clone.position += glm::vec3{worldOffset, 0.0F, worldOffset};
            m_map.props.push_back(clone);
            newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
        }

        if (newIndices.empty())
        {
            m_statusLine = "Paste failed";
            return;
        }
        ClearSelections();
        m_selectedProps = newIndices;
        m_selection = Selection{SelectionKind::Prop, newIndices.back()};
        m_clipboard.pasteCount += 1;
        m_statusLine = "Pasted prop(s): " + std::to_string(newIndices.size());
    }
}

void LevelEditor::AutoComputeLoopBoundsAndFootprint()
{
    if (m_loop.elements.empty())
    {
        return;
    }

    glm::vec3 minValue{1.0e9F};
    glm::vec3 maxValue{-1.0e9F};
    for (const LoopElement& element : m_loop.elements)
    {
        minValue = glm::min(minValue, element.position - element.halfExtents);
        maxValue = glm::max(maxValue, element.position + element.halfExtents);
    }

    if (!m_loop.manualBounds)
    {
        m_loop.boundsMin = minValue;
        m_loop.boundsMax = maxValue;
    }

    if (!m_loop.manualFootprint)
    {
        const glm::vec3 size = maxValue - minValue;
        m_loop.footprintWidth = std::max(1, static_cast<int>(std::ceil(size.x / kEditorTileSize)));
        m_loop.footprintHeight = std::max(1, static_cast<int>(std::ceil(size.z / kEditorTileSize)));
    }
}

std::vector<std::string> LevelEditor::ValidateLoopForUi() const
{
    return LevelAssetIO::ValidateLoop(m_loop);
}

std::string LevelEditor::BuildUniqueLoopElementName(const std::string& preferredBaseName) const
{
    const std::string base = StripNumericSuffix(preferredBaseName.empty() ? "element" : preferredBaseName);
    int suffix = 1;
    while (true)
    {
        const std::string candidate = base + "_" + std::to_string(suffix);
        const bool exists = std::any_of(
            m_loop.elements.begin(),
            m_loop.elements.end(),
            [&](const LoopElement& element) { return element.name == candidate; });
        if (!exists)
        {
            return candidate;
        }
        ++suffix;
    }
}

std::string LevelEditor::BuildUniquePropName(const std::string& preferredBaseName) const
{
    const std::string base = StripNumericSuffix(preferredBaseName.empty() ? "prop" : preferredBaseName);
    int suffix = 1;
    while (true)
    {
        const std::string candidate = base + "_" + std::to_string(suffix);
        const bool exists = std::any_of(
            m_map.props.begin(),
            m_map.props.end(),
            [&](const PropInstance& prop) { return prop.name == candidate; });
        if (!exists)
        {
            return candidate;
        }
        ++suffix;
    }
}

void LevelEditor::RefreshContentBrowser()
{
    m_contentEntries = m_assetRegistry.ListDirectory(m_contentDirectory);
    if (m_selectedContentEntry >= static_cast<int>(m_contentEntries.size()))
    {
        m_selectedContentEntry = -1;
        m_selectedContentPath.clear();
    }
    m_contentNeedsRefresh = false;
}

void LevelEditor::PlaceImportedAssetAtHovered(const std::string& relativeAssetPath)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Asset placement requires Map Editor + hovered tile";
        return;
    }

    PushHistorySnapshot();

    PropInstance prop;
    prop.name = BuildUniquePropName(std::filesystem::path(relativeAssetPath).stem().string());
    prop.type = PropType::MeshAsset;
    prop.meshAsset = relativeAssetPath;
    prop.materialAsset.clear();
    prop.position = m_hoveredWorld + glm::vec3{0.0F, 0.8F, 0.0F};
    prop.halfExtents = glm::vec3{0.8F, 0.8F, 0.8F};
    prop.colliderHalfExtents = prop.halfExtents;
    prop.colliderType = ColliderType::Box;
    if (m_gridSnap)
    {
        prop.position.x = std::round(prop.position.x / m_gridStep) * m_gridStep;
        prop.position.z = std::round(prop.position.z / m_gridStep) * m_gridStep;
    }

    m_map.props.push_back(prop);
    SelectSingle(Selection{SelectionKind::Prop, static_cast<int>(m_map.props.size()) - 1});
    m_statusLine = "Placed asset " + relativeAssetPath;
}

void LevelEditor::InstantiatePrefabAtHovered(const std::string& prefabId)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Prefab instantiate requires Map Editor + hovered tile";
        return;
    }

    PrefabAsset prefab;
    std::string error;
    if (!LevelAssetIO::LoadPrefab(prefabId, &prefab, &error))
    {
        m_statusLine = "Load prefab failed: " + error;
        return;
    }
    if (prefab.props.empty())
    {
        m_statusLine = "Prefab is empty.";
        return;
    }

    PushHistorySnapshot();
    const std::string instanceId = prefab.id + "_inst_" + std::to_string(m_nextPrefabInstanceId++);
    std::vector<int> newIndices;
    newIndices.reserve(prefab.props.size());
    for (const PropInstance& src : prefab.props)
    {
        PropInstance prop = src;
        prop.name = BuildUniquePropName(src.name.empty() ? "prop" : src.name);
        prop.position += m_hoveredWorld;
        prop.prefabSourceId = prefab.id;
        prop.prefabInstanceId = instanceId;
        m_map.props.push_back(prop);
        newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
    }

    m_selectedProps = newIndices;
    m_selection = Selection{SelectionKind::Prop, newIndices.back()};
    m_statusLine = "Instantiated prefab " + prefab.id;
}

void LevelEditor::SaveSelectedPropsAsPrefab(const std::string& prefabId)
{
    if (m_mode != Mode::MapEditor)
    {
        m_statusLine = "Save prefab available only in Map Editor";
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
    if (indices.empty())
    {
        m_statusLine = "Select at least one prop to create prefab";
        return;
    }

    glm::vec3 pivot{0.0F};
    for (int idx : indices)
    {
        pivot += m_map.props[static_cast<std::size_t>(idx)].position;
    }
    pivot /= static_cast<float>(indices.size());

    PrefabAsset prefab;
    prefab.id = prefabId;
    prefab.displayName = prefabId;
    for (int idx : indices)
    {
        PropInstance copy = m_map.props[static_cast<std::size_t>(idx)];
        copy.position -= pivot;
        copy.prefabSourceId.clear();
        copy.prefabInstanceId.clear();
        prefab.props.push_back(copy);
    }

    std::string error;
    if (LevelAssetIO::SavePrefab(prefab, &error))
    {
        RefreshLibraries();
        m_statusLine = "Saved prefab " + prefab.id;
        return;
    }
    m_statusLine = "Save prefab failed: " + error;
}

void LevelEditor::ReapplySelectedPrefabInstance()
{
    if (m_mode != Mode::MapEditor)
    {
        m_statusLine = "Reapply prefab available only in Map Editor";
        return;
    }

    const std::vector<int> selected = SortedUniqueValidSelection(SelectionKind::Prop);
    if (selected.empty())
    {
        m_statusLine = "Select prefab instance props first";
        return;
    }

    const PropInstance& seed = m_map.props[static_cast<std::size_t>(selected.front())];
    if (seed.prefabSourceId.empty() || seed.prefabInstanceId.empty())
    {
        m_statusLine = "Selected prop is not a prefab instance";
        return;
    }

    PrefabAsset prefab;
    std::string error;
    if (!LevelAssetIO::LoadPrefab(seed.prefabSourceId, &prefab, &error))
    {
        m_statusLine = "Load prefab failed: " + error;
        return;
    }

    std::vector<int> instanceIndices;
    glm::vec3 anchor{0.0F};
    for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
    {
        const PropInstance& prop = m_map.props[static_cast<std::size_t>(i)];
        if (prop.prefabInstanceId == seed.prefabInstanceId)
        {
            instanceIndices.push_back(i);
            anchor += prop.position;
        }
    }
    if (instanceIndices.empty())
    {
        m_statusLine = "Prefab instance not found in map";
        return;
    }
    anchor /= static_cast<float>(instanceIndices.size());

    PushHistorySnapshot();
    for (auto it = instanceIndices.rbegin(); it != instanceIndices.rend(); ++it)
    {
        m_map.props.erase(m_map.props.begin() + *it);
    }

    std::vector<int> newIndices;
    for (const PropInstance& src : prefab.props)
    {
        PropInstance prop = src;
        prop.position += anchor;
        prop.prefabSourceId = prefab.id;
        prop.prefabInstanceId = seed.prefabInstanceId;
        prop.name = BuildUniquePropName(src.name.empty() ? "prop" : src.name);
        m_map.props.push_back(prop);
        newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
    }

    m_selectedProps = newIndices;
    m_selection = Selection{SelectionKind::Prop, newIndices.empty() ? -1 : newIndices.back()};
    m_statusLine = "Reapplied prefab instance " + seed.prefabInstanceId;
}

const MaterialAsset* LevelEditor::GetMaterialCached(const std::string& materialId) const
{
    if (materialId.empty())
    {
        return nullptr;
    }

    const auto cached = m_materialCache.find(materialId);
    if (cached != m_materialCache.end())
    {
        return &cached->second;
    }

    MaterialAsset loaded;
    if (!LevelAssetIO::LoadMaterial(materialId, &loaded, nullptr))
    {
        return nullptr;
    }

    const auto [insertedIt, _] = m_materialCache.emplace(materialId, std::move(loaded));
    return &insertedIt->second;
}

const AnimationClipAsset* LevelEditor::GetAnimationClipCached(const std::string& clipId) const
{
    if (clipId.empty())
    {
        return nullptr;
    }

    const auto cached = m_animationCache.find(clipId);
    if (cached != m_animationCache.end())
    {
        return &cached->second;
    }

    AnimationClipAsset loaded;
    if (!LevelAssetIO::LoadAnimationClip(clipId, &loaded, nullptr))
    {
        return nullptr;
    }

    const auto [insertedIt, _] = m_animationCache.emplace(clipId, std::move(loaded));
    return &insertedIt->second;
}

std::string LevelEditor::SelectedLabel() const
{
    switch (m_selection.kind)
    {
        case SelectionKind::None: return "None";
        case SelectionKind::LoopElement:
            if (m_selectedLoopElements.size() > 1)
            {
                return "Loop elements (" + std::to_string(m_selectedLoopElements.size()) + ")";
            }
            return "Loop element #" + std::to_string(m_selection.index);
        case SelectionKind::MapPlacement:
            if (m_selectedMapPlacements.size() > 1)
            {
                return "Placements (" + std::to_string(m_selectedMapPlacements.size()) + ")";
            }
            return "Placement #" + std::to_string(m_selection.index);
        case SelectionKind::Prop:
            if (m_selectedProps.size() > 1)
            {
                return "Props (" + std::to_string(m_selectedProps.size()) + ")";
            }
            return "Prop #" + std::to_string(m_selection.index);
        default: return "None";
    }
}

void LevelEditor::Update(
    float deltaSeconds,
    const engine::platform::Input& input,
    bool controlsEnabled,
    int framebufferWidth,
    int framebufferHeight
)
{
    if (m_contentNeedsRefresh)
    {
        RefreshContentBrowser();
    }

    if (m_animationPreviewPlaying)
    {
        float speed = 1.0F;
        if (m_selection.kind == SelectionKind::Prop &&
            m_selection.index >= 0 &&
            m_selection.index < static_cast<int>(m_map.props.size()))
        {
            speed = std::max(0.01F, m_map.props[static_cast<std::size_t>(m_selection.index)].animationSpeed);
        }
        m_animationPreviewTime += deltaSeconds * speed;
    }

    HandleCamera(deltaSeconds, input, controlsEnabled);
    HandleEditorHotkeys(input, controlsEnabled);
    ApplyGizmoInput(input, deltaSeconds);
    UpdateHoveredTile(input, framebufferWidth, framebufferHeight);
    if (m_axisDragActive && input.IsMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        StopAxisDrag();
    }

    if (!controlsEnabled)
    {
        return;
    }

#if BUILD_WITH_IMGUI
    if (ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }
#endif

    glm::vec3 rayOrigin{0.0F};
    glm::vec3 rayDirection{0.0F};
    if (!BuildMouseRay(input, framebufferWidth, framebufferHeight, &rayOrigin, &rayDirection))
    {
        return;
    }

    if (m_axisDragActive)
    {
        if (input.IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
        {
            UpdateAxisDrag(rayOrigin, rayDirection);
        }
        return;
    }

    if (input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        if (m_mode == Mode::MapEditor && m_lightPlacementMode)
        {
            AddLightAtHovered(m_lightPlacementType);
            return;
        }

        if (StartAxisDrag(rayOrigin, rayDirection))
        {
            return;
        }

        const bool ctrlDown = input.IsKeyDown(GLFW_KEY_LEFT_CONTROL) || input.IsKeyDown(GLFW_KEY_RIGHT_CONTROL);
        int pickedLightIndex = -1;
        float pickedLightT = 1.0e9F;
        if (m_mode == Mode::MapEditor)
        {
            for (int i = 0; i < static_cast<int>(m_map.lights.size()); ++i)
            {
                const LightInstance& light = m_map.lights[static_cast<std::size_t>(i)];
                const glm::vec3 extents = light.type == LightType::Spot ? glm::vec3{0.28F, 0.28F, 0.28F} : glm::vec3{0.24F, 0.24F, 0.24F};
                float t = 0.0F;
                if (!SegmentIntersectsAabb(rayOrigin, rayDirection, light.position - extents, light.position + extents, &t))
                {
                    continue;
                }
                if (t < pickedLightT)
                {
                    pickedLightT = t;
                    pickedLightIndex = i;
                }
            }
        }
        if (pickedLightIndex >= 0)
        {
            if (!ctrlDown)
            {
                ClearSelections();
            }
            m_selectedLightIndex = pickedLightIndex;
            m_statusLine = "Selected light " + m_map.lights[static_cast<std::size_t>(pickedLightIndex)].name;
            return;
        }

        const Selection selection = PickSelection(rayOrigin, rayDirection);
        if (selection.kind != SelectionKind::None)
        {
            m_selectedLightIndex = -1;
            if (ctrlDown)
            {
                ToggleSelection(selection);
            }
            else
            {
                SelectSingle(selection);
            }
            return;
        }

        if (!ctrlDown)
        {
            ClearSelections();
            m_selectedLightIndex = -1;
        }

        if (m_mode == Mode::MapEditor)
        {
            if (m_propPlacementMode)
            {
                AddPropAtHoveredTile();
            }
            else
            {
                PlaceLoopAtHoveredTile();
            }
        }
    }

    if (input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && m_mode == Mode::MapEditor)
    {
        RemovePlacementAtHoveredTile();
    }
}

void LevelEditor::Render(engine::render::Renderer& renderer) const
{
    const bool loopMode = m_mode == Mode::LoopEditor;
    if (loopMode)
    {
        renderer.SetPointLights({});
        renderer.SetSpotLights({});
    }
    else
    {
        std::vector<engine::render::PointLight> pointLights;
        std::vector<engine::render::SpotLight> spotLights;
        pointLights.reserve(m_map.lights.size());
        spotLights.reserve(m_map.lights.size());

        for (const LightInstance& light : m_map.lights)
        {
            if (!light.enabled)
            {
                continue;
            }

            if (light.type == LightType::Spot)
            {
                const glm::mat3 rotation = RotationMatrixFromEulerDegrees(light.rotationEuler);
                const glm::vec3 dir = glm::normalize(rotation * glm::vec3{0.0F, 0.0F, -1.0F});
                const float inner = std::cos(glm::radians(glm::clamp(light.spotInnerAngle, 1.0F, 89.0F)));
                const float outer = std::cos(glm::radians(glm::clamp(light.spotOuterAngle, light.spotInnerAngle + 0.1F, 89.5F)));
                spotLights.push_back(engine::render::SpotLight{
                    light.position,
                    dir,
                    glm::clamp(light.color, glm::vec3{0.0F}, glm::vec3{10.0F}),
                    glm::max(0.0F, light.intensity),
                    glm::max(0.1F, light.range),
                    inner,
                    outer,
                });
            }
            else
            {
                pointLights.push_back(engine::render::PointLight{
                    light.position,
                    glm::clamp(light.color, glm::vec3{0.0F}, glm::vec3{10.0F}),
                    glm::max(0.0F, light.intensity),
                    glm::max(0.1F, light.range),
                });
            }
        }

        renderer.SetPointLights(pointLights);
        renderer.SetSpotLights(spotLights);
    }

    const int gridHalf = loopMode ? static_cast<int>(kEditorTileSize * 0.5F) : std::max(8, std::max(m_map.width, m_map.height));
    const float step = loopMode ? 1.0F : m_map.tileSize;
    const glm::vec3 majorColor = m_debugView ? glm::vec3{0.35F, 0.35F, 0.35F} : glm::vec3{0.18F, 0.18F, 0.18F};
    const glm::vec3 minorColor = m_debugView ? glm::vec3{0.18F, 0.18F, 0.18F} : glm::vec3{0.1F, 0.1F, 0.1F};
    renderer.DrawGrid(gridHalf, step, majorColor, minorColor);

    if (loopMode)
    {
        const float halfTile = kEditorTileSize * 0.5F;
        renderer.DrawBox(glm::vec3{0.0F, 0.005F, 0.0F}, glm::vec3{halfTile, 0.005F, halfTile}, glm::vec3{0.12F, 0.14F, 0.17F});
        const glm::vec3 edgeColor{1.0F, 0.95F, 0.35F};
        renderer.DrawOverlayLine(glm::vec3{-halfTile, 0.02F, -halfTile}, glm::vec3{halfTile, 0.02F, -halfTile}, edgeColor);
        renderer.DrawOverlayLine(glm::vec3{halfTile, 0.02F, -halfTile}, glm::vec3{halfTile, 0.02F, halfTile}, edgeColor);
        renderer.DrawOverlayLine(glm::vec3{halfTile, 0.02F, halfTile}, glm::vec3{-halfTile, 0.02F, halfTile}, edgeColor);
        renderer.DrawOverlayLine(glm::vec3{-halfTile, 0.02F, halfTile}, glm::vec3{-halfTile, 0.02F, -halfTile}, edgeColor);
    }

    if (m_debugView)
    {
        renderer.DrawOverlayLine(glm::vec3{0.0F, 0.01F, 0.0F}, glm::vec3{4.0F, 0.01F, 0.0F}, glm::vec3{1.0F, 0.25F, 0.25F});
        renderer.DrawOverlayLine(glm::vec3{0.0F, 0.01F, 0.0F}, glm::vec3{0.0F, 4.0F, 0.0F}, glm::vec3{0.25F, 1.0F, 0.25F});
        renderer.DrawOverlayLine(glm::vec3{0.0F, 0.01F, 0.0F}, glm::vec3{0.0F, 0.01F, 4.0F}, glm::vec3{0.25F, 0.55F, 1.0F});
    }

    auto drawGizmo = [&]() {
        if (m_selection.kind == SelectionKind::None)
        {
            return;
        }
        if (m_gizmoMode == GizmoMode::Scale && m_selection.kind == SelectionKind::MapPlacement)
        {
            return;
        }
        if (m_gizmoMode == GizmoMode::Rotate && m_selection.kind == SelectionKind::MapPlacement)
        {
            return;
        }

        const glm::vec3 pivot = SelectionPivot();
        const float cameraDistance = glm::length(m_cameraPosition - pivot);
        const float axisLength = glm::clamp(cameraDistance * 0.18F, 1.8F, 10.0F);
        const float headSize = glm::max(0.12F, axisLength * 0.08F);
        const float arrowHeadLength = glm::max(0.25F, axisLength * 0.2F);
        const float arrowHeadWidth = glm::max(0.1F, arrowHeadLength * 0.35F);

        auto axisColor = [&](GizmoAxis axis) {
            const bool active = m_axisDragActive && m_axisDragAxis == axis;
            if (axis == GizmoAxis::X)
            {
                return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{1.0F, 0.25F, 0.25F};
            }
            if (axis == GizmoAxis::Y)
            {
                return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{0.25F, 1.0F, 0.25F};
            }
            return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{0.25F, 0.55F, 1.0F};
        };

        auto drawAxisLine = [&](const glm::vec3& direction, GizmoAxis axis, bool drawArrow, bool drawCube) {
            if (m_selection.kind == SelectionKind::MapPlacement && axis == GizmoAxis::Y)
            {
                return;
            }
            const glm::vec3 color = axisColor(axis);
            const glm::vec3 dir = glm::normalize(direction);
            const glm::vec3 tip = pivot + dir * axisLength;
            renderer.DrawOverlayLine(pivot, tip, color);

            if (drawArrow)
            {
                glm::vec3 side = glm::cross(dir, glm::vec3{0.0F, 1.0F, 0.0F});
                if (glm::length(side) < 1.0e-4F)
                {
                    side = glm::cross(dir, glm::vec3{1.0F, 0.0F, 0.0F});
                }
                side = glm::normalize(side);
                const glm::vec3 up = glm::normalize(glm::cross(side, dir));
                const glm::vec3 base = tip - dir * arrowHeadLength;
                renderer.DrawOverlayLine(tip, base + side * arrowHeadWidth, color);
                renderer.DrawOverlayLine(tip, base - side * arrowHeadWidth, color);
                renderer.DrawOverlayLine(tip, base + up * arrowHeadWidth, color);
                renderer.DrawOverlayLine(tip, base - up * arrowHeadWidth, color);
            }
            if (drawCube)
            {
                renderer.DrawBox(tip, glm::vec3{headSize}, color);
            }
            else
            {
                renderer.DrawBox(tip, glm::vec3{headSize * 0.75F}, color);
            }
        };

        if (m_gizmoMode == GizmoMode::Rotate)
        {
            drawAxisLine(glm::vec3{1.0F, 0.0F, 0.0F}, GizmoAxis::X, false, true);
            drawAxisLine(glm::vec3{0.0F, 1.0F, 0.0F}, GizmoAxis::Y, false, true);
            drawAxisLine(glm::vec3{0.0F, 0.0F, 1.0F}, GizmoAxis::Z, false, true);
            return;
        }

        const bool arrow = m_gizmoMode == GizmoMode::Translate;
        const bool cube = m_gizmoMode == GizmoMode::Scale;
        drawAxisLine(glm::vec3{1.0F, 0.0F, 0.0F}, GizmoAxis::X, arrow, cube);
        drawAxisLine(glm::vec3{0.0F, 1.0F, 0.0F}, GizmoAxis::Y, arrow, cube);
        drawAxisLine(glm::vec3{0.0F, 0.0F, 1.0F}, GizmoAxis::Z, arrow, cube);
    };

    if (loopMode)
    {
        for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
        {
            const LoopElement& element = m_loop.elements[static_cast<std::size_t>(i)];
            glm::vec3 color{0.8F, 0.8F, 0.8F};
            if (element.type == LoopElementType::Window)
            {
                color = glm::vec3{0.2F, 0.8F, 1.0F};
            }
            else if (element.type == LoopElementType::Pallet)
            {
                color = glm::vec3{1.0F, 0.85F, 0.2F};
            }
            else if (element.type == LoopElementType::Marker)
            {
                color = glm::vec3{0.9F, 0.4F, 1.0F};
            }
            if (IsSelected(SelectionKind::LoopElement, i))
            {
                color = glm::vec3{1.0F, 0.2F, 0.2F};
            }
            if (element.transformLocked && !IsSelected(SelectionKind::LoopElement, i))
            {
                color *= 0.65F;
            }
            renderer.DrawOrientedBox(element.position, element.halfExtents, ElementRotation(element), color);
        }

        if (m_debugView)
        {
            const glm::vec3 loopCenter = (m_loop.boundsMin + m_loop.boundsMax) * 0.5F;
            const glm::vec3 loopHalf = glm::max(glm::vec3{0.05F}, (m_loop.boundsMax - m_loop.boundsMin) * 0.5F);
            renderer.DrawBox(loopCenter + glm::vec3{0.0F, 0.01F, 0.0F}, loopHalf, glm::vec3{0.35F, 0.65F, 0.35F});
        }

        drawGizmo();
        return;
    }

    for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
    {
        const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset loop;
        std::string error;
        if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
        {
            continue;
        }

        const glm::ivec2 footprint = RotatedFootprint(loop, placement.rotationDegrees);
        const glm::vec3 pivot = TileCenter(placement.tileX, placement.tileY) +
                                glm::vec3{
                                    (static_cast<float>(footprint.x) - 1.0F) * m_map.tileSize * 0.5F,
                                    0.0F,
                                    (static_cast<float>(footprint.y) - 1.0F) * m_map.tileSize * 0.5F,
                                };

        for (const LoopElement& element : loop.elements)
        {
            const glm::vec3 worldCenter = pivot + RotateY(element.position, static_cast<float>(placement.rotationDegrees));
            const glm::vec3 worldRotation{
                element.pitchDegrees,
                static_cast<float>(placement.rotationDegrees) + element.yawDegrees,
                element.rollDegrees,
            };
            glm::vec3 color{0.55F, 0.55F, 0.58F};
            if (element.type == LoopElementType::Window)
            {
                color = glm::vec3{0.2F, 0.8F, 1.0F};
            }
            else if (element.type == LoopElementType::Pallet)
            {
                color = glm::vec3{1.0F, 0.85F, 0.2F};
            }
            if (IsSelected(SelectionKind::MapPlacement, i))
            {
                color = glm::vec3{1.0F, 0.3F, 0.3F};
            }
            renderer.DrawOrientedBox(worldCenter, element.halfExtents, worldRotation, color);
        }

        if (m_debugView)
        {
            renderer.DrawBox(
                pivot + glm::vec3{0.0F, 0.02F, 0.0F},
                glm::vec3{
                    static_cast<float>(footprint.x) * m_map.tileSize * 0.5F,
                    0.02F,
                    static_cast<float>(footprint.y) * m_map.tileSize * 0.5F,
                },
                glm::vec3{0.4F, 0.4F, 0.4F}
            );
        }
    }

    for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
    {
        const PropInstance& prop = m_map.props[static_cast<std::size_t>(i)];
        glm::vec3 color{0.3F, 0.6F, 0.28F};
        if (prop.type == PropType::Rock)
        {
            color = glm::vec3{0.5F, 0.5F, 0.55F};
        }
        else if (prop.type == PropType::Obstacle)
        {
            color = glm::vec3{0.75F, 0.38F, 0.28F};
        }
        else if (prop.type == PropType::Platform)
        {
            color = glm::vec3{0.62F, 0.62F, 0.70F};
        }
        else if (prop.type == PropType::MeshAsset)
        {
            color = glm::vec3{0.45F, 0.75F, 0.92F};
        }
        if (!prop.materialAsset.empty())
        {
            if (const MaterialAsset* material = GetMaterialCached(prop.materialAsset); material != nullptr)
            {
                color = glm::vec3{material->baseColor.r, material->baseColor.g, material->baseColor.b};
                if (material->shaderType == MaterialShaderType::Unlit)
                {
                    color *= 1.1F;
                }
                color = glm::clamp(color, glm::vec3{0.0F}, glm::vec3{1.0F});
            }
        }
        if (IsSelected(SelectionKind::Prop, i))
        {
            color = glm::vec3{1.0F, 0.3F, 0.3F};
        }
        if (prop.transformLocked && !IsSelected(SelectionKind::Prop, i))
        {
            color *= 0.65F;
        }

        glm::vec3 drawPosition = prop.position;
        glm::vec3 drawRotation = PropRotation(prop);
        glm::vec3 drawScale = glm::vec3{1.0F};
        const bool isSelectedProp = IsSelected(SelectionKind::Prop, i);
        if (!prop.animationClip.empty() && ((isSelectedProp && m_animationPreviewPlaying) || prop.animationAutoplay))
        {
            if (const AnimationClipAsset* cachedClip = GetAnimationClipCached(prop.animationClip); cachedClip != nullptr)
            {
                AnimationClipAsset clip = *cachedClip;
                clip.loop = clip.loop && prop.animationLoop;
                clip.speed *= std::max(0.01F, prop.animationSpeed);
                glm::vec3 posOffset{0.0F};
                glm::vec3 rotOffset{0.0F};
                if (SampleAnimation(clip, m_animationPreviewTime * std::max(0.01F, clip.speed), &posOffset, &rotOffset, &drawScale))
                {
                    drawPosition += posOffset;
                    drawRotation += rotOffset;
                }
            }
        }

        renderer.DrawOrientedBox(drawPosition, prop.halfExtents * drawScale, drawRotation, color);
        if (prop.type == PropType::MeshAsset && !prop.meshAsset.empty())
        {
            std::string loadError;
            const std::filesystem::path absolute = m_assetRegistry.AbsolutePath(prop.meshAsset);
            const engine::assets::MeshData* meshData = m_meshLibrary.LoadMesh(absolute, &loadError);
            if (meshData != nullptr && meshData->loaded)
            {
                const glm::vec3 meshSize = glm::max(glm::vec3{0.0001F}, meshData->boundsMax - meshData->boundsMin);
                const glm::vec3 targetSize = glm::max(glm::vec3{0.05F}, prop.halfExtents * 2.0F);
                const glm::vec3 meshScale{
                    targetSize.x / meshSize.x,
                    targetSize.y / meshSize.y,
                    targetSize.z / meshSize.z,
                };
                renderer.DrawMesh(meshData->geometry, drawPosition, drawRotation, meshScale * drawScale, color);
            }
            else if (m_debugView && !loadError.empty())
            {
                renderer.DrawOverlayLine(
                    drawPosition,
                    drawPosition + glm::vec3{0.0F, 2.4F, 0.0F},
                    glm::vec3{1.0F, 0.2F, 0.2F}
                );
            }
        }

        if (m_debugView && prop.type == PropType::MeshAsset)
        {
            renderer.DrawOverlayLine(drawPosition, drawPosition + glm::vec3{0.0F, 1.8F, 0.0F}, glm::vec3{0.35F, 0.9F, 1.0F});
        }
    }

    for (int i = 0; i < static_cast<int>(m_map.lights.size()); ++i)
    {
        const LightInstance& light = m_map.lights[static_cast<std::size_t>(i)];
        const bool selected = m_selectedLightIndex == i;
        glm::vec3 color = glm::clamp(light.color, glm::vec3{0.05F}, glm::vec3{1.0F});
        if (!light.enabled)
        {
            color *= 0.35F;
        }
        if (selected)
        {
            color = glm::vec3{1.0F, 0.35F, 0.2F};
        }

        const float markerRadius = light.type == LightType::Spot ? 0.22F : 0.18F;
        renderer.DrawCapsule(light.position, markerRadius * 2.0F, markerRadius, color);
        if (light.type == LightType::Spot || m_debugView)
        {
            const glm::mat3 rotation = RotationMatrixFromEulerDegrees(light.rotationEuler);
            const glm::vec3 dir = glm::normalize(rotation * glm::vec3{0.0F, 0.0F, -1.0F});
            const float lineLength = glm::max(1.0F, light.range * (light.type == LightType::Spot ? 0.25F : 0.12F));
            renderer.DrawOverlayLine(light.position, light.position + dir * lineLength, color);
        }
    }

    if (m_hoveredTileValid && m_debugView)
    {
        const glm::vec3 center = TileCenter(m_hoveredTile.x, m_hoveredTile.y);
        const glm::vec3 color = m_propPlacementMode
                                    ? glm::vec3{0.7F, 0.3F, 1.0F}
                                    : (CanPlaceLoopAt(m_hoveredTile.x, m_hoveredTile.y, m_pendingPlacementRotation, -1)
                                           ? glm::vec3{0.25F, 1.0F, 0.25F}
                                           : glm::vec3{1.0F, 0.25F, 0.25F});
        renderer.DrawBox(center + glm::vec3{0.0F, 0.02F, 0.0F}, glm::vec3{m_map.tileSize * 0.5F, 0.02F, m_map.tileSize * 0.5F}, color);
    }

    if (m_mode == Mode::MapEditor && m_lightPlacementMode && m_hoveredTileValid)
    {
        const glm::vec3 center = TileCenter(m_hoveredTile.x, m_hoveredTile.y);
        const bool spot = m_lightPlacementType == LightType::Spot;
        const glm::vec3 color = spot ? glm::vec3{1.0F, 0.65F, 0.2F} : glm::vec3{1.0F, 1.0F, 0.4F};
        const glm::vec3 pos = center + glm::vec3{0.0F, spot ? 3.0F : 2.5F, 0.0F};
        renderer.DrawOverlayLine(center + glm::vec3{0.0F, 0.05F, 0.0F}, pos, color);
        renderer.DrawCapsule(pos, 0.42F, 0.18F, color);
        if (spot)
        {
            const glm::vec3 dir = glm::normalize(RotationMatrixFromEulerDegrees(glm::vec3{-45.0F, glm::degrees(m_cameraYaw), 0.0F}) * glm::vec3{0.0F, 0.0F, -1.0F});
            renderer.DrawOverlayLine(pos, pos + dir * 2.4F, color);
        }
    }

    drawGizmo();
}

glm::mat4 LevelEditor::BuildViewProjection(float aspectRatio) const
{
    const glm::vec3 forward = CameraForward();
    const glm::mat4 view = glm::lookAt(m_cameraPosition, m_cameraPosition + forward, CameraUp());
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), aspectRatio > 0.0F ? aspectRatio : (16.0F / 9.0F), 0.05F, 900.0F);
    return projection * view;
}

engine::render::EnvironmentSettings LevelEditor::CurrentEnvironmentSettings() const
{
    return ToRenderEnvironment(m_environmentEditing);
}

void LevelEditor::DrawUi(
    bool* outBackToMenu,
    bool* outPlaytestMap,
    std::string* outPlaytestMapName
)
{
#if BUILD_WITH_IMGUI
    if (outBackToMenu != nullptr)
    {
        *outBackToMenu = false;
    }
    if (outPlaytestMap != nullptr)
    {
        *outPlaytestMap = false;
    }
    if (outPlaytestMapName != nullptr)
    {
        outPlaytestMapName->clear();
    }

    auto saveCurrentLoop = [this]() {
        std::string error;
        if (LevelAssetIO::SaveLoop(m_loop, &error))
        {
            m_statusLine = "Saved loop " + m_loop.id;
            RefreshLibraries();
        }
        else
        {
            m_statusLine = "Save failed: " + error;
        }
    };

    auto saveCurrentMap = [this]() {
        std::string error;
        if (m_map.environmentAssetId.empty())
        {
            m_map.environmentAssetId = "default_environment";
        }
        if (!LevelAssetIO::SaveEnvironment(m_environmentEditing, &error))
        {
            m_statusLine = "Save environment failed: " + error;
            return;
        }
        m_map.environmentAssetId = m_environmentEditing.id;
        if (LevelAssetIO::SaveMap(m_map, &error))
        {
            m_statusLine = "Saved map " + m_map.name;
            RefreshLibraries();
        }
        else
        {
            m_statusLine = "Save map failed: " + error;
        }
    };

    ImGui::SetNextWindowBgAlpha(0.85F);
    ImGui::SetNextWindowPos(ImVec2(14.0F, 14.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Editor Mode", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Mode: %s", ModeToText(m_mode));
        ImGui::SameLine();
        if (ImGui::Button("Loop Editor"))
        {
            m_mode = Mode::LoopEditor;
        }
        ImGui::SameLine();
        if (ImGui::Button("Map Editor"))
        {
            m_mode = Mode::MapEditor;
        }
        ImGui::Separator();
        ImGui::Text("Camera Speed: %.1f", m_cameraSpeed);
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 1.0e-4F && !ImGui::GetIO().WantCaptureMouse)
        {
            m_cameraSpeed = glm::clamp(m_cameraSpeed + wheel * 2.0F, 2.0F, 120.0F);
        }
        ImGui::Checkbox("Top-down View", &m_topDownView);
        ImGui::SameLine();
        ImGui::Text("(%s)", m_topDownView ? "ON" : "OFF");
        ImGui::Checkbox("Grid Snap", &m_gridSnap);
        ImGui::SameLine();
        ImGui::Text("(%s)", m_gridSnap ? "ON" : "OFF");
        ImGui::DragFloat("Grid Step", &m_gridStep, 0.05F, 0.1F, 8.0F);
        ImGui::Checkbox("Angle Snap", &m_angleSnap);
        ImGui::SameLine();
        ImGui::Text("(%s)", m_angleSnap ? "ON" : "OFF");
        ImGui::DragFloat("Angle Step", &m_angleStepDegrees, 1.0F, 1.0F, 90.0F);
        int renderModeIndex = m_currentRenderMode == engine::render::RenderMode::Wireframe ? 0 : 1;
        const char* renderModeItems[] = {"Wireframe", "Filled"};
        if (ImGui::Combo("Viewport Render", &renderModeIndex, renderModeItems, IM_ARRAYSIZE(renderModeItems)))
        {
            m_pendingRenderMode =
                renderModeIndex == 0 ? engine::render::RenderMode::Wireframe : engine::render::RenderMode::Filled;
            m_currentRenderMode = *m_pendingRenderMode;
        }
        const bool hasEnabledLights = std::any_of(
            m_map.lights.begin(),
            m_map.lights.end(),
            [](const LightInstance& light) { return light.enabled; }
        );
        ImGui::Checkbox("Auto Lit Preview", &m_autoLitPreview);
        if (m_autoLitPreview && m_mode == Mode::MapEditor && hasEnabledLights &&
            m_currentRenderMode != engine::render::RenderMode::Filled)
        {
            m_pendingRenderMode = engine::render::RenderMode::Filled;
            m_currentRenderMode = engine::render::RenderMode::Filled;
        }
        if (m_mode == Mode::MapEditor && hasEnabledLights &&
            m_currentRenderMode != engine::render::RenderMode::Filled)
        {
            ImGui::TextColored(ImVec4(1.0F, 0.82F, 0.25F, 1.0F), "Lights visible only in Filled mode");
            if (ImGui::Button("Switch To Filled (Lighting)"))
            {
                m_pendingRenderMode = engine::render::RenderMode::Filled;
                m_currentRenderMode = engine::render::RenderMode::Filled;
            }
        }
        ImGui::Checkbox("Debug View", &m_debugView);
        ImGui::SameLine();
        ImGui::Text("(%s)", m_debugView ? "ON" : "OFF");
        ImGui::Text("Gizmo: %s (1/2/3)", GizmoToText(m_gizmoMode));
        ImGui::Text("Render Mode: %s", RenderModeToText(m_currentRenderMode));
        ImGui::Text("Axis Drag: %s", m_axisDragActive ? "ACTIVE (LMB hold)" : "READY");
        if (m_mode == Mode::LoopEditor)
        {
            ImGui::Text("Loop Tile Boundaries: ON (16 units)");
        }
        if (m_mode == Mode::MapEditor)
        {
            ImGui::Text("Prop Placement: %s", m_propPlacementMode ? "ON" : "OFF");
        }
        ImGui::Text("Selected: %s", SelectedLabel().c_str());
        ImGui::TextWrapped("%s", m_statusLine.c_str());
        auto setSelectionLocked = [this](bool locked) {
            if (m_selection.kind == SelectionKind::LoopElement)
            {
                const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
                for (int idx : indices)
                {
                    m_loop.elements[static_cast<std::size_t>(idx)].transformLocked = locked;
                }
                return;
            }
            if (m_selection.kind == SelectionKind::MapPlacement)
            {
                const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
                for (int idx : indices)
                {
                    m_map.placements[static_cast<std::size_t>(idx)].transformLocked = locked;
                }
                return;
            }
            if (m_selection.kind == SelectionKind::Prop)
            {
                const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
                for (int idx : indices)
                {
                    m_map.props[static_cast<std::size_t>(idx)].transformLocked = locked;
                }
            }
        };
        if (ImGui::Button("Lock Selected"))
        {
            setSelectionLocked(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Unlock Selected"))
        {
            setSelectionLocked(false);
        }
        if (ImGui::Button("Copy Selected"))
        {
            CopyCurrentSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Paste Clipboard"))
        {
            PasteClipboard();
        }
        if (ImGui::Button("Undo (Ctrl+Z)"))
        {
            Undo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo (Ctrl+Y)"))
        {
            Redo();
        }
        ImGui::Text("History: %d undo / %d redo", static_cast<int>(m_undoStack.size()), static_cast<int>(m_redoStack.size()));
        ImGui::Separator();
        ImGui::TextUnformatted("Hotkeys:");
        ImGui::TextUnformatted("RMB+Mouse look | WASD/QE fly | Wheel speed");
        ImGui::TextUnformatted("1/2/3 gizmo | LMB handle drag (move/rotate/scale)");
        ImGui::TextUnformatted("Rotate: click X/Y/Z handle in rotate gizmo");
        ImGui::TextUnformatted("Arrows/PgUp/PgDn keyboard nudge");
        ImGui::TextUnformatted("[/] rotate | +/- scale | G snap | T top-down");
        ImGui::TextUnformatted("F2 debug view | F3 toggle wireframe/filled");
        ImGui::TextUnformatted("R rotate placement | P prop mode");
        ImGui::TextUnformatted("Ctrl+C copy | Ctrl+V paste");
        ImGui::TextUnformatted("Ctrl+D duplicate | Del delete | Ctrl+Click multi-select");

        if (ImGui::Button("Back To Main Menu"))
        {
            if (outBackToMenu != nullptr)
            {
                *outBackToMenu = true;
            }
        }
        if (m_mode == Mode::MapEditor)
        {
            ImGui::SameLine();
            if (ImGui::Button("Playtest Current Map"))
            {
                std::string error;
                if (LevelAssetIO::SaveEnvironment(m_environmentEditing, &error))
                {
                    m_map.environmentAssetId = m_environmentEditing.id;
                }
                if (error.empty() && LevelAssetIO::SaveMap(m_map, &error))
                {
                    if (outPlaytestMap != nullptr)
                    {
                        *outPlaytestMap = true;
                    }
                    if (outPlaytestMapName != nullptr)
                    {
                        *outPlaytestMapName = m_map.name;
                    }
                }
                else
                {
                    m_statusLine = "Playtest failed: " + error;
                }
            }
        }
    }
    ImGui::End();

    if (m_mode == Mode::LoopEditor)
    {
        ImGui::SetNextWindowPos(ImVec2(14.0F, 300.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Loop Library"))
        {
            char searchBuffer[128]{};
            std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", m_loopSearch.c_str());
            if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer)))
            {
                m_loopSearch = searchBuffer;
            }

            if (ImGui::Button("Refresh"))
            {
                RefreshLibraries();
            }
            ImGui::SameLine();
            if (ImGui::Button("New"))
            {
                PushHistorySnapshot();
                CreateNewLoop("new_loop");
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Current"))
            {
                saveCurrentLoop();
            }
            if (ImGui::Button("Load Selected") && m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()))
            {
                LoopAsset loaded;
                std::string error;
                const std::string& id = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                if (LevelAssetIO::LoadLoop(id, &loaded, &error))
                {
                    PushHistorySnapshot();
                    m_loop = loaded;
                    ClearSelections();
                    m_statusLine = "Loaded loop " + id;
                }
                else
                {
                    m_statusLine = "Load failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate Selected") && m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()))
            {
                LoopAsset loaded;
                std::string error;
                const std::string sourceId = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                if (LevelAssetIO::LoadLoop(sourceId, &loaded, &error))
                {
                    loaded.id = sourceId + "_copy";
                    loaded.displayName = loaded.displayName + " Copy";
                    if (LevelAssetIO::SaveLoop(loaded, &error))
                    {
                        RefreshLibraries();
                        m_statusLine = "Duplicated loop " + sourceId;
                    }
                    else
                    {
                        m_statusLine = "Duplicate failed: " + error;
                    }
                }
            }
            if (ImGui::Button("Delete Selected") && m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()))
            {
                std::string error;
                const std::string id = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                if (LevelAssetIO::DeleteLoop(id, &error))
                {
                    RefreshLibraries();
                    m_selectedLibraryLoop = -1;
                    m_statusLine = "Deleted loop " + id;
                }
                else
                {
                    m_statusLine = "Delete failed: " + error;
                }
            }

            char renameBuffer[128]{};
            std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", m_loopRenameTarget.c_str());
            if (ImGui::InputText("Rename To", renameBuffer, sizeof(renameBuffer)))
            {
                m_loopRenameTarget = renameBuffer;
            }
            if (ImGui::Button("Rename Selected") &&
                m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()) &&
                !m_loopRenameTarget.empty())
            {
                std::string error;
                const std::string oldId = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                LoopAsset loaded;
                if (LevelAssetIO::LoadLoop(oldId, &loaded, &error))
                {
                    loaded.id = m_loopRenameTarget;
                    loaded.displayName = m_loopRenameTarget;
                    if (LevelAssetIO::SaveLoop(loaded, &error))
                    {
                        const bool deletedOld = LevelAssetIO::DeleteLoop(oldId, nullptr);
                        (void)deletedOld;
                        RefreshLibraries();
                        m_statusLine = "Renamed loop " + oldId + " -> " + m_loopRenameTarget;
                        if (m_loop.id == oldId)
                        {
                            PushHistorySnapshot();
                            m_loop = loaded;
                        }
                    }
                    else
                    {
                        m_statusLine = "Rename failed: " + error;
                    }
                }
            }

            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(m_loopLibrary.size()); ++i)
            {
                const std::string& id = m_loopLibrary[static_cast<std::size_t>(i)];
                if (!ContainsCaseInsensitive(id, m_loopSearch))
                {
                    continue;
                }
                const bool selected = m_selectedLibraryLoop == i;
                if (ImGui::Selectable(id.c_str(), selected))
                {
                    m_selectedLibraryLoop = i;
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(390.0F, 300.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Loop Editor"))
        {
            ImGui::TextWrapped("Quick Guide (Loop Editor):");
            ImGui::BulletText("Add Wall/Window/Pallet/Marker.");
            ImGui::BulletText("Select object with LMB (Ctrl+LMB for multiselect).");
            ImGui::BulletText("Use gizmo mode 1/2/3 then drag axis handle.");
            ImGui::BulletText("1 tile area (16x16) is always visible with strong border.");
            ImGui::BulletText("Save Current to reusable loop asset.");
            ImGui::Separator();
            char idBuffer[128]{};
            std::snprintf(idBuffer, sizeof(idBuffer), "%s", m_loop.id.c_str());
            if (ImGui::InputText("Loop ID", idBuffer, sizeof(idBuffer)))
            {
                m_loop.id = idBuffer;
            }
            char displayNameBuffer[128]{};
            std::snprintf(displayNameBuffer, sizeof(displayNameBuffer), "%s", m_loop.displayName.c_str());
            if (ImGui::InputText("Display Name", displayNameBuffer, sizeof(displayNameBuffer)))
            {
                m_loop.displayName = displayNameBuffer;
            }

            ImGui::Checkbox("Manual Bounds", &m_loop.manualBounds);
            if (m_loop.manualBounds)
            {
                ImGui::DragFloat3("Bounds Min", &m_loop.boundsMin.x, 0.1F);
                ImGui::DragFloat3("Bounds Max", &m_loop.boundsMax.x, 0.1F);
            }
            ImGui::Checkbox("Manual Footprint", &m_loop.manualFootprint);
            if (m_loop.manualFootprint)
            {
                ImGui::InputInt("Footprint Width", &m_loop.footprintWidth);
                ImGui::InputInt("Footprint Height", &m_loop.footprintHeight);
                m_loop.footprintWidth = std::max(1, m_loop.footprintWidth);
                m_loop.footprintHeight = std::max(1, m_loop.footprintHeight);
            }
            else
            {
                ImGui::Text("Footprint: %d x %d", m_loop.footprintWidth, m_loop.footprintHeight);
            }

            if (ImGui::Button("Auto Compute Bounds/Footprint"))
            {
                AutoComputeLoopBoundsAndFootprint();
            }

            ImGui::Separator();
            if (ImGui::Button("Add Wall"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Wall;
                element.name = BuildUniqueLoopElementName("wall");
                element.position = glm::vec3{0.0F, 1.0F, 0.0F};
                element.halfExtents = glm::vec3{1.0F, 1.0F, 0.2F};
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Window"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Window;
                element.name = BuildUniqueLoopElementName("window");
                element.position = glm::vec3{0.0F, 1.0F, 0.0F};
                element.halfExtents = glm::vec3{0.8F, 0.9F, 0.2F};
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Pallet"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Pallet;
                element.name = BuildUniqueLoopElementName("pallet");
                element.position = glm::vec3{0.0F, 0.8F, 0.0F};
                element.halfExtents = glm::vec3{0.8F, 0.8F, 0.25F};
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Marker"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Marker;
                element.name = BuildUniqueLoopElementName("marker");
                element.position = glm::vec3{0.0F, 0.5F, 0.0F};
                element.halfExtents = glm::vec3{0.2F, 0.2F, 0.2F};
                element.markerTag = "survivor_spawn";
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }

            ImGui::Separator();
            ImGui::Text("Elements: %d", static_cast<int>(m_loop.elements.size()));
            if (ImGui::BeginListBox("##loop_elements", ImVec2(-1.0F, 170.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
                {
                    const LoopElement& element = m_loop.elements[static_cast<std::size_t>(i)];
                    const std::string label =
                        element.name + " [" + LoopElementTypeToText(element.type) + "]" + (element.transformLocked ? " [LOCK]" : "");
                    const bool selected = IsSelected(SelectionKind::LoopElement, i);
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        if (ImGui::GetIO().KeyCtrl)
                        {
                            ToggleSelection(Selection{SelectionKind::LoopElement, i});
                        }
                        else
                        {
                            SelectSingle(Selection{SelectionKind::LoopElement, i});
                        }
                    }
                }
                ImGui::EndListBox();
            }

            ImGui::Separator();
            const std::vector<std::string> issues = ValidateLoopForUi();
            if (issues.empty())
            {
                ImGui::TextColored(ImVec4(0.35F, 1.0F, 0.35F, 1.0F), "Validation: OK");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.2F, 1.0F), "Validation:");
                for (const std::string& issue : issues)
                {
                    ImGui::BulletText("%s", issue.c_str());
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(930.0F, 300.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Inspector"))
        {
            if (m_selection.kind == SelectionKind::LoopElement &&
                m_selection.index >= 0 && m_selection.index < static_cast<int>(m_loop.elements.size()))
            {
                LoopElement& element = m_loop.elements[static_cast<std::size_t>(m_selection.index)];
                ImGui::Text("Element #%d", m_selection.index);

                char nameBuffer[128]{};
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", element.name.c_str());
                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                {
                    element.name = nameBuffer;
                }

                int typeIndex = static_cast<int>(element.type);
                const char* typeItems[] = {"Wall", "Window", "Pallet", "Marker"};
                if (ImGui::Combo("Type", &typeIndex, typeItems, IM_ARRAYSIZE(typeItems)))
                {
                    typeIndex = std::clamp(typeIndex, 0, 3);
                    element.type = static_cast<LoopElementType>(typeIndex);
                }
                ImGui::DragFloat3("Position", &element.position.x, 0.05F);
                ImGui::DragFloat3("Half Extents", &element.halfExtents.x, 0.05F, 0.05F, 64.0F);
                ImGui::DragFloat3("Rotation (Pitch/Yaw/Roll)", &element.pitchDegrees, 1.0F, -720.0F, 720.0F);
                ImGui::Checkbox("Lock Transform", &element.transformLocked);
                if (element.type == LoopElementType::Marker || element.type == LoopElementType::Window)
                {
                    char tagBuffer[128]{};
                    std::snprintf(tagBuffer, sizeof(tagBuffer), "%s", element.markerTag.c_str());
                    if (ImGui::InputText("Marker/Meta", tagBuffer, sizeof(tagBuffer)))
                    {
                        element.markerTag = tagBuffer;
                    }
                }
                if (ImGui::Button("Delete Element"))
                {
                    DeleteCurrentSelection();
                }
                AutoComputeLoopBoundsAndFootprint();
            }
            else
            {
                ImGui::TextUnformatted("Select a loop element.");
            }
        }
        ImGui::End();
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(14.0F, 300.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Map Library"))
        {
            char searchBuffer[128]{};
            std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", m_mapSearch.c_str());
            if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer)))
            {
                m_mapSearch = searchBuffer;
            }
            if (ImGui::Button("Refresh"))
            {
                RefreshLibraries();
            }
            ImGui::SameLine();
            if (ImGui::Button("New Map"))
            {
                PushHistorySnapshot();
                CreateNewMap("new_map");
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Current"))
            {
                saveCurrentMap();
            }
            if (ImGui::Button("Load Selected") && m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()))
            {
                MapAsset loaded;
                std::string error;
                const std::string& name = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                if (LevelAssetIO::LoadMap(name, &loaded, &error))
                {
                    PushHistorySnapshot();
                    m_map = loaded;
                    m_selectedLightIndex = m_map.lights.empty() ? -1 : 0;
                    (void)LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr);
                    ClearSelections();
                    m_statusLine = "Loaded map " + name;
                }
                else
                {
                    m_statusLine = "Load map failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate Selected") && m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()))
            {
                MapAsset loaded;
                std::string error;
                const std::string sourceName = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                if (LevelAssetIO::LoadMap(sourceName, &loaded, &error))
                {
                    loaded.name = sourceName + "_copy";
                    if (LevelAssetIO::SaveMap(loaded, &error))
                    {
                        RefreshLibraries();
                        m_statusLine = "Duplicated map " + sourceName;
                    }
                }
            }
            if (ImGui::Button("Delete Selected") && m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()))
            {
                std::string error;
                const std::string name = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                if (LevelAssetIO::DeleteMap(name, &error))
                {
                    RefreshLibraries();
                    m_selectedLibraryMap = -1;
                    m_statusLine = "Deleted map " + name;
                }
                else
                {
                    m_statusLine = "Delete map failed: " + error;
                }
            }

            char renameBuffer[128]{};
            std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", m_mapRenameTarget.c_str());
            if (ImGui::InputText("Rename To", renameBuffer, sizeof(renameBuffer)))
            {
                m_mapRenameTarget = renameBuffer;
            }
            if (ImGui::Button("Rename Selected") &&
                m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()) &&
                !m_mapRenameTarget.empty())
            {
                std::string error;
                const std::string oldName = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                MapAsset loaded;
                if (LevelAssetIO::LoadMap(oldName, &loaded, &error))
                {
                    loaded.name = m_mapRenameTarget;
                    if (LevelAssetIO::SaveMap(loaded, &error))
                    {
                        const bool deletedOld = LevelAssetIO::DeleteMap(oldName, nullptr);
                        (void)deletedOld;
                        RefreshLibraries();
                        m_statusLine = "Renamed map " + oldName + " -> " + m_mapRenameTarget;
                        if (m_map.name == oldName)
                        {
                            PushHistorySnapshot();
                            m_map = loaded;
                            (void)LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr);
                        }
                    }
                }
            }
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(m_mapLibrary.size()); ++i)
            {
                const std::string& name = m_mapLibrary[static_cast<std::size_t>(i)];
                if (!ContainsCaseInsensitive(name, m_mapSearch))
                {
                    continue;
                }
                const bool selected = m_selectedLibraryMap == i;
                if (ImGui::Selectable(name.c_str(), selected))
                {
                    m_selectedLibraryMap = i;
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(410.0F, 300.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(330.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Loop Palette"))
        {
            if (ImGui::Button("Refresh Loops"))
            {
                RefreshLibraries();
            }
            ImGui::Text("Selected Loop: %s",
                        (m_paletteLoopIndex >= 0 && m_paletteLoopIndex < static_cast<int>(m_loopLibrary.size()))
                            ? m_loopLibrary[static_cast<std::size_t>(m_paletteLoopIndex)].c_str()
                            : "none");
            if (ImGui::BeginListBox("##loop_palette", ImVec2(-1.0F, 360.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_loopLibrary.size()); ++i)
                {
                    const bool selected = m_paletteLoopIndex == i;
                    if (ImGui::Selectable(m_loopLibrary[static_cast<std::size_t>(i)].c_str(), selected))
                    {
                        m_paletteLoopIndex = i;
                    }
                }
                ImGui::EndListBox();
            }
            ImGui::Text("Pending Rotation: %d (R key)", m_pendingPlacementRotation);
            if (ImGui::Button("Place At Hovered"))
            {
                PlaceLoopAtHoveredTile();
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove At Hovered"))
            {
                RemovePlacementAtHoveredTile();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(410.0F, 870.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(330.0F, 270.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Prefabs"))
        {
            char prefabIdBuffer[128]{};
            std::snprintf(prefabIdBuffer, sizeof(prefabIdBuffer), "%s", m_prefabNewId.c_str());
            if (ImGui::InputText("Prefab Id", prefabIdBuffer, sizeof(prefabIdBuffer)))
            {
                m_prefabNewId = prefabIdBuffer;
            }

            if (ImGui::Button("Save Selected Props As Prefab"))
            {
                SaveSelectedPropsAsPrefab(m_prefabNewId);
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh Prefabs"))
            {
                RefreshLibraries();
            }

            if (ImGui::BeginListBox("##prefab_library", ImVec2(-1.0F, 130.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_prefabLibrary.size()); ++i)
                {
                    const bool selected = m_selectedPrefabIndex == i;
                    if (ImGui::Selectable(m_prefabLibrary[static_cast<std::size_t>(i)].c_str(), selected))
                    {
                        m_selectedPrefabIndex = i;
                    }
                }
                ImGui::EndListBox();
            }

            if (m_selectedPrefabIndex >= 0 && m_selectedPrefabIndex < static_cast<int>(m_prefabLibrary.size()))
            {
                const std::string& selectedPrefab = m_prefabLibrary[static_cast<std::size_t>(m_selectedPrefabIndex)];
                ImGui::Text("Selected: %s", selectedPrefab.c_str());
                if (ImGui::Button("Instantiate At Hovered"))
                {
                    InstantiatePrefabAtHovered(selectedPrefab);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Prefab"))
                {
                    std::string error;
                    if (LevelAssetIO::DeletePrefab(selectedPrefab, &error))
                    {
                        m_statusLine = "Deleted prefab " + selectedPrefab;
                        RefreshLibraries();
                    }
                    else
                    {
                        m_statusLine = "Delete prefab failed: " + error;
                    }
                }
            }

            if (ImGui::Button("Reapply Selected Prefab Instance"))
            {
                ReapplySelectedPrefabInstance();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(750.0F, 300.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Map Editor"))
        {
            ImGui::TextWrapped("Quick Guide (Level/Map Editor):");
            ImGui::BulletText("Select loop from Loop Palette and place on hovered tile.");
            ImGui::BulletText("R rotates pending loop, P toggles prop placement, L toggles light placement.");
            ImGui::BulletText("Props/placements can be selected and transformed.");
            ImGui::BulletText("Quick Loop Objects places Wall/Window/Pallet/Marker as ready 1x1 loop prefabs.");
            ImGui::BulletText("Add Point/Spot lights in Lights section and tune them in Inspector.");
            ImGui::BulletText("Debug View shows extra overlays and placement validation.");
            ImGui::TextWrapped("Place mode: %s", m_propPlacementMode ? "PROP" : "LOOP");
            ImGui::Separator();
            char mapNameBuffer[128]{};
            std::snprintf(mapNameBuffer, sizeof(mapNameBuffer), "%s", m_map.name.c_str());
            if (ImGui::InputText("Map Name", mapNameBuffer, sizeof(mapNameBuffer)))
            {
                m_map.name = mapNameBuffer;
            }
            ImGui::InputInt("Width", &m_map.width);
            ImGui::InputInt("Height", &m_map.height);
            m_map.width = std::max(4, m_map.width);
            m_map.height = std::max(4, m_map.height);
            ImGui::DragFloat("Tile Size", &m_map.tileSize, 0.5F, 4.0F, 64.0F);
            m_map.tileSize = std::max(4.0F, m_map.tileSize);
            ImGui::DragFloat3("Survivor Spawn", &m_map.survivorSpawn.x, 0.1F);
            ImGui::DragFloat3("Killer Spawn", &m_map.killerSpawn.x, 0.1F);
            char environmentBuffer[128]{};
            std::snprintf(environmentBuffer, sizeof(environmentBuffer), "%s", m_map.environmentAssetId.c_str());
            if (ImGui::InputText("Environment Asset", environmentBuffer, sizeof(environmentBuffer)))
            {
                m_map.environmentAssetId = environmentBuffer;
            }
            if (ImGui::Button("Load Environment Asset"))
            {
                if (LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr))
                {
                    m_statusLine = "Loaded environment " + m_map.environmentAssetId;
                }
                else
                {
                    m_statusLine = "Failed to load environment " + m_map.environmentAssetId;
                }
            }
            ImGui::Separator();
            ImGui::Text(
                "Placements: %d | Props: %d | Lights: %d",
                static_cast<int>(m_map.placements.size()),
                static_cast<int>(m_map.props.size()),
                static_cast<int>(m_map.lights.size())
            );
            ImGui::Text("Hovered Tile: %s", m_hoveredTileValid ? "valid" : "none");
            if (m_hoveredTileValid)
            {
                ImGui::Text("Tile: (%d, %d)", m_hoveredTile.x, m_hoveredTile.y);
                const glm::vec3 base = TileCenter(m_hoveredTile.x, m_hoveredTile.y);
                ImGui::Text("Hovered World: %.2f %.2f %.2f", base.x, base.y, base.z);
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Lights");
            const bool lightPlacementBefore = m_lightPlacementMode;
            ImGui::Checkbox("Light Placement Mode (LMB)", &m_lightPlacementMode);
            if (!lightPlacementBefore && m_lightPlacementMode)
            {
                m_propPlacementMode = false;
            }
            ImGui::SameLine();
            int lightPlacementType = m_lightPlacementType == LightType::Spot ? 1 : 0;
            const char* lightTypeItems[] = {"Point", "Spot"};
            if (ImGui::Combo("Light Type", &lightPlacementType, lightTypeItems, IM_ARRAYSIZE(lightTypeItems)))
            {
                m_lightPlacementType = lightPlacementType == 1 ? LightType::Spot : LightType::Point;
            }
            if (m_lightPlacementMode)
            {
                ImGui::TextWrapped("LMB places %s light at hovered tile center.", m_lightPlacementType == LightType::Spot ? "Spot" : "Point");
                if (m_hoveredTileValid)
                {
                    const glm::vec3 pos = TileCenter(m_hoveredTile.x, m_hoveredTile.y) +
                                          glm::vec3{0.0F, m_lightPlacementType == LightType::Spot ? 3.0F : 2.5F, 0.0F};
                    ImGui::Text("Preview Pos: %.2f %.2f %.2f", pos.x, pos.y, pos.z);
                }
                else
                {
                    ImGui::TextUnformatted("Move cursor over map tile to place light");
                }
            }
            if (ImGui::Button("Add Point Light"))
            {
                AddLightAtHovered(LightType::Point);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Spot Light"))
            {
                AddLightAtHovered(LightType::Spot);
            }
            if (ImGui::BeginListBox("##map_lights", ImVec2(-1.0F, 90.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_map.lights.size()); ++i)
                {
                    const LightInstance& light = m_map.lights[static_cast<std::size_t>(i)];
                    const bool selected = m_selectedLightIndex == i;
                    std::string label = light.name + " [" + LightTypeToText(light.type) + "]";
                    if (!light.enabled)
                    {
                        label += " (off)";
                    }
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        m_selectedLightIndex = i;
                    }
                }
                ImGui::EndListBox();
            }
            if (m_selectedLightIndex >= 0 && m_selectedLightIndex < static_cast<int>(m_map.lights.size()))
            {
                if (ImGui::Button("Delete Selected Light"))
                {
                    PushHistorySnapshot();
                    m_map.lights.erase(m_map.lights.begin() + m_selectedLightIndex);
                    if (m_map.lights.empty())
                    {
                        m_selectedLightIndex = -1;
                    }
                    else
                    {
                        m_selectedLightIndex = std::min(m_selectedLightIndex, static_cast<int>(m_map.lights.size()) - 1);
                    }
                    m_statusLine = "Deleted light";
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Quick Loop Objects (1x1)");
            int quickLoopType = static_cast<int>(m_quickLoopType);
            const char* quickLoopItems[] = {"Wall", "Window", "Pallet", "Marker"};
            if (ImGui::Combo("Loop Object", &quickLoopType, quickLoopItems, IM_ARRAYSIZE(quickLoopItems)))
            {
                quickLoopType = std::clamp(quickLoopType, 0, 3);
                m_quickLoopType = static_cast<LoopElementType>(quickLoopType);
            }
            ImGui::TextWrapped("Use this to quickly place a single loop object on hovered tile without switching editor mode.");
            if (ImGui::Button("Place Loop Object At Hovered"))
            {
                PlaceQuickLoopObjectAtHovered(m_quickLoopType);
            }
            const bool propPlacementBefore = m_propPlacementMode;
            ImGui::Checkbox("Prop Placement Mode (P)", &m_propPlacementMode);
            if (!propPlacementBefore && m_propPlacementMode)
            {
                m_lightPlacementMode = false;
            }
            if (m_propPlacementMode)
            {
                int propIndex = static_cast<int>(m_selectedPropType);
                const char* propItems[] = {"Rock", "Tree", "Obstacle", "Platform", "MeshAsset"};
                if (ImGui::Combo("Prop Type", &propIndex, propItems, IM_ARRAYSIZE(propItems)))
                {
                    propIndex = std::clamp(propIndex, 0, 4);
                    m_selectedPropType = static_cast<PropType>(propIndex);
                }
                if (ImGui::Button("Add Prop At Hovered"))
                {
                    AddPropAtHoveredTile();
                }
            }
            if (ImGui::Button("Add Small Platform At Hovered"))
            {
                m_selectedPropType = PropType::Platform;
                AddPropAtHoveredTile();
            }
            ImGui::Separator();
            ImGui::TextWrapped("Drag mesh asset from Content Browser and drop below:");
            ImGui::Button("Scene Drop Target", ImVec2(-1.0F, 26.0F));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
                {
                    const char* path = static_cast<const char*>(payload->Data);
                    if (path != nullptr)
                    {
                        PlaceImportedAssetAtHovered(path);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(1120.0F, 300.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Inspector"))
        {
            if (m_selection.kind == SelectionKind::MapPlacement &&
                m_selection.index >= 0 && m_selection.index < static_cast<int>(m_map.placements.size()))
            {
                LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(m_selection.index)];
                ImGui::Text("Placement #%d", m_selection.index);
                ImGui::Text("Loop: %s", placement.loopId.c_str());
                ImGui::InputInt("Tile X", &placement.tileX);
                ImGui::InputInt("Tile Y", &placement.tileY);
                ImGui::SliderInt("Rotation", &placement.rotationDegrees, 0, 270, "%d deg");
                placement.rotationDegrees = ((placement.rotationDegrees + 45) / 90) * 90;
                placement.rotationDegrees = ((placement.rotationDegrees % 360) + 360) % 360;
                ImGui::Checkbox("Lock Transform", &placement.transformLocked);
                if (ImGui::Button("Delete Placement"))
                {
                    DeleteCurrentSelection();
                }
            }
            else if (m_selection.kind == SelectionKind::Prop &&
                     m_selection.index >= 0 && m_selection.index < static_cast<int>(m_map.props.size()))
            {
                PropInstance& prop = m_map.props[static_cast<std::size_t>(m_selection.index)];
                ImGui::Text("Prop #%d", m_selection.index);
                int propIndex = static_cast<int>(prop.type);
                const char* propItems[] = {"Rock", "Tree", "Obstacle", "Platform", "MeshAsset"};
                if (ImGui::Combo("Type", &propIndex, propItems, IM_ARRAYSIZE(propItems)))
                {
                    propIndex = std::clamp(propIndex, 0, 4);
                    prop.type = static_cast<PropType>(propIndex);
                }
                char propNameBuffer[128]{};
                std::snprintf(propNameBuffer, sizeof(propNameBuffer), "%s", prop.name.c_str());
                if (ImGui::InputText("Name", propNameBuffer, sizeof(propNameBuffer)))
                {
                    prop.name = propNameBuffer;
                }
                ImGui::DragFloat3("Position", &prop.position.x, 0.05F);
                ImGui::DragFloat3("Half Extents", &prop.halfExtents.x, 0.05F, 0.05F, 64.0F);
                ImGui::DragFloat3("Rotation (Pitch/Yaw/Roll)", &prop.pitchDegrees, 1.0F, -720.0F, 720.0F);
                char meshBuffer[256]{};
                std::snprintf(meshBuffer, sizeof(meshBuffer), "%s", prop.meshAsset.c_str());
                if (ImGui::InputText("Mesh Asset", meshBuffer, sizeof(meshBuffer)))
                {
                    prop.meshAsset = meshBuffer;
                }
                char materialBuffer[256]{};
                std::snprintf(materialBuffer, sizeof(materialBuffer), "%s", prop.materialAsset.c_str());
                if (ImGui::InputText("Material Asset", materialBuffer, sizeof(materialBuffer)))
                {
                    prop.materialAsset = materialBuffer;
                }
                char animBuffer[256]{};
                std::snprintf(animBuffer, sizeof(animBuffer), "%s", prop.animationClip.c_str());
                if (ImGui::InputText("Animation Clip", animBuffer, sizeof(animBuffer)))
                {
                    prop.animationClip = animBuffer;
                }
                ImGui::Checkbox("Anim Loop", &prop.animationLoop);
                ImGui::Checkbox("Anim AutoPlay", &prop.animationAutoplay);
                ImGui::DragFloat("Anim Speed", &prop.animationSpeed, 0.05F, 0.01F, 8.0F);
                int colliderType = static_cast<int>(prop.colliderType);
                const char* colliderItems[] = {"None", "Box", "Capsule"};
                if (ImGui::Combo("Collider Type", &colliderType, colliderItems, IM_ARRAYSIZE(colliderItems)))
                {
                    colliderType = std::clamp(colliderType, 0, 2);
                    prop.colliderType = static_cast<ColliderType>(colliderType);
                }
                ImGui::DragFloat3("Collider Offset", &prop.colliderOffset.x, 0.05F);
                ImGui::DragFloat3("Collider HalfExt", &prop.colliderHalfExtents.x, 0.05F, 0.05F, 64.0F);
                ImGui::DragFloat("Collider Radius", &prop.colliderRadius, 0.01F, 0.05F, 8.0F);
                ImGui::DragFloat("Collider Height", &prop.colliderHeight, 0.01F, 0.1F, 16.0F);
                ImGui::Checkbox("Lock Transform", &prop.transformLocked);
                ImGui::Checkbox("Solid", &prop.solid);
                if (ImGui::Button(m_animationPreviewPlaying ? "Stop Preview Animation" : "Play Preview Animation"))
                {
                    m_animationPreviewPlaying = !m_animationPreviewPlaying;
                    if (!m_animationPreviewPlaying)
                    {
                        m_animationPreviewTime = 0.0F;
                    }
                }
                if (ImGui::Button("Delete Prop"))
                {
                    DeleteCurrentSelection();
                }
            }
            else if (m_selectedLightIndex >= 0 && m_selectedLightIndex < static_cast<int>(m_map.lights.size()))
            {
                LightInstance& light = m_map.lights[static_cast<std::size_t>(m_selectedLightIndex)];
                ImGui::Text("Light #%d", m_selectedLightIndex);
                char lightNameBuffer[128]{};
                std::snprintf(lightNameBuffer, sizeof(lightNameBuffer), "%s", light.name.c_str());
                if (ImGui::InputText("Light Name", lightNameBuffer, sizeof(lightNameBuffer)))
                {
                    light.name = lightNameBuffer;
                }
                int typeIndex = light.type == LightType::Spot ? 1 : 0;
                const char* typeItems[] = {"Point", "Spot"};
                if (ImGui::Combo("Light Type", &typeIndex, typeItems, IM_ARRAYSIZE(typeItems)))
                {
                    light.type = typeIndex == 1 ? LightType::Spot : LightType::Point;
                }
                ImGui::Checkbox("Enabled", &light.enabled);
                ImGui::ColorEdit3("Light Color", &light.color.x);
                ImGui::DragFloat3("Light Position", &light.position.x, 0.05F);
                ImGui::DragFloat("Intensity", &light.intensity, 0.05F, 0.0F, 64.0F);
                ImGui::DragFloat("Range", &light.range, 0.1F, 0.1F, 256.0F);
                if (light.type == LightType::Spot)
                {
                    ImGui::DragFloat3("Rotation (Pitch/Yaw/Roll)", &light.rotationEuler.x, 1.0F, -720.0F, 720.0F);
                    ImGui::DragFloat("Inner Angle", &light.spotInnerAngle, 0.2F, 1.0F, 89.0F);
                    ImGui::DragFloat("Outer Angle", &light.spotOuterAngle, 0.2F, 1.5F, 89.5F);
                    light.spotOuterAngle = std::max(light.spotOuterAngle, light.spotInnerAngle + 0.1F);
                }
            }
            else
            {
                ImGui::TextUnformatted("Select map placement, prop or light.");
            }
        }
        ImGui::End();
    }

    ImGui::SetNextWindowPos(ImVec2(1460.0F, 300.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0F, 560.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Content Browser"))
        {
        if (m_contentNeedsRefresh)
        {
            RefreshContentBrowser();
        }

        ImGui::Text("Directory: %s", m_contentDirectory.c_str());
        if (ImGui::Button("Up"))
        {
            if (m_contentDirectory != "." && !m_contentDirectory.empty())
            {
                std::filesystem::path p = m_contentDirectory;
                const std::filesystem::path parent = p.parent_path();
                m_contentDirectory = parent.empty() ? "." : parent.generic_string();
                m_contentNeedsRefresh = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Assets"))
        {
            RefreshLibraries();
            RefreshContentBrowser();
        }

        char importBuffer[260]{};
        std::snprintf(importBuffer, sizeof(importBuffer), "%s", m_contentImportPath.c_str());
        if (ImGui::InputText("Import Path", importBuffer, sizeof(importBuffer)))
        {
            m_contentImportPath = importBuffer;
        }
        if (ImGui::Button("Import File"))
        {
            const engine::assets::ImportResult imported = m_assetRegistry.ImportExternalFile(m_contentImportPath);
            m_statusLine = imported.success ? imported.message : ("Import failed: " + imported.message);
            RefreshContentBrowser();
        }

        char folderBuffer[128]{};
        std::snprintf(folderBuffer, sizeof(folderBuffer), "%s", m_contentNewFolderName.c_str());
        if (ImGui::InputText("New Folder", folderBuffer, sizeof(folderBuffer)))
        {
            m_contentNewFolderName = folderBuffer;
        }
        if (ImGui::Button("Create Folder"))
        {
            const std::string relative = (m_contentDirectory == "." ? "" : (m_contentDirectory + "/")) + m_contentNewFolderName;
            std::string error;
            if (m_assetRegistry.CreateFolder(relative, &error))
            {
                m_statusLine = "Created folder " + relative;
                RefreshContentBrowser();
            }
            else
            {
                m_statusLine = "Create folder failed: " + error;
            }
        }

        char renameBuffer[256]{};
        std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", m_contentRenameTarget.c_str());
        if (ImGui::InputText("Rename Selected To", renameBuffer, sizeof(renameBuffer)))
        {
            m_contentRenameTarget = renameBuffer;
        }
        if (ImGui::Button("Rename Selected") && m_selectedContentEntry >= 0 && m_selectedContentEntry < static_cast<int>(m_contentEntries.size()))
        {
            const std::string from = m_contentEntries[static_cast<std::size_t>(m_selectedContentEntry)].relativePath;
            std::filesystem::path targetPath = std::filesystem::path(from).parent_path() / m_contentRenameTarget;
            std::string error;
            if (m_assetRegistry.RenamePath(from, targetPath.generic_string(), &error))
            {
                m_statusLine = "Renamed asset";
                RefreshContentBrowser();
            }
            else
            {
                m_statusLine = "Rename failed: " + error;
            }
        }
        if (ImGui::Button("Delete Selected") && m_selectedContentEntry >= 0 && m_selectedContentEntry < static_cast<int>(m_contentEntries.size()))
        {
            std::string error;
            if (m_assetRegistry.DeletePath(m_contentEntries[static_cast<std::size_t>(m_selectedContentEntry)].relativePath, &error))
            {
                m_statusLine = "Deleted asset";
                m_selectedContentEntry = -1;
                m_selectedContentPath.clear();
                RefreshContentBrowser();
            }
            else
            {
                m_statusLine = "Delete failed: " + error;
            }
        }

        ImGui::Separator();
        if (ImGui::BeginListBox("##content_entries", ImVec2(-1.0F, 260.0F)))
        {
            for (int i = 0; i < static_cast<int>(m_contentEntries.size()); ++i)
            {
                const auto& entry = m_contentEntries[static_cast<std::size_t>(i)];
                std::string label = std::string(entry.directory ? "[DIR] " : "[") + (entry.directory ? "" : AssetKindToText(entry.kind)) + (entry.directory ? "" : "] ") + entry.name;
                const bool selected = m_selectedContentEntry == i;
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    m_selectedContentEntry = i;
                    m_selectedContentPath = entry.relativePath;
                }
                if (ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered() && entry.directory)
                {
                    m_contentDirectory = entry.relativePath;
                    m_contentNeedsRefresh = true;
                }

                if (!entry.directory)
                {
                    if (ImGui::BeginDragDropSource())
                    {
                        const std::string payloadPath = entry.relativePath;
                        ImGui::SetDragDropPayload("CONTENT_ASSET_PATH", payloadPath.c_str(), payloadPath.size() + 1);
                        ImGui::Text("Drop: %s", payloadPath.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }
            ImGui::EndListBox();
        }

        ImGui::TextWrapped("Selected: %s", m_selectedContentPath.empty() ? "none" : m_selectedContentPath.c_str());
        if (!m_selectedContentPath.empty())
        {
            const engine::assets::AssetKind kind = engine::assets::AssetRegistry::KindFromPath(std::filesystem::path(m_selectedContentPath));
            ImGui::Text("Kind: %s", AssetKindToText(kind));
            if (kind == engine::assets::AssetKind::Mesh)
            {
                std::string err;
                const engine::assets::MeshData* md = m_meshLibrary.LoadMesh(m_assetRegistry.AbsolutePath(m_selectedContentPath), &err);
                if (md != nullptr && md->loaded)
                {
                    const glm::vec3 size = md->boundsMax - md->boundsMin;
                    ImGui::Text("Mesh verts: %d tris: %d",
                                static_cast<int>(md->geometry.positions.size()),
                                static_cast<int>(md->geometry.indices.size() / 3));
                    ImGui::Text("Bounds size: %.2f %.2f %.2f", size.x, size.y, size.z);
                }
                else if (!err.empty())
                {
                    ImGui::TextWrapped("Mesh load: %s", err.c_str());
                }
            }
        }
        if (m_mode == Mode::MapEditor && !m_selectedContentPath.empty() && ImGui::Button("Place Selected Asset At Hovered"))
        {
            PlaceImportedAssetAtHovered(m_selectedContentPath);
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(1460.0F, 875.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0F, 390.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Materials & Environment"))
    {
        if (ImGui::CollapsingHeader("Material Editor", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Material Library");
            if (ImGui::BeginListBox("##material_library", ImVec2(-1.0F, 90.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_materialLibrary.size()); ++i)
                {
                    const bool selected = m_selectedMaterialIndex == i;
                    if (ImGui::Selectable(m_materialLibrary[static_cast<std::size_t>(i)].c_str(), selected))
                    {
                        m_selectedMaterialIndex = i;
                        m_selectedMaterialId = m_materialLibrary[static_cast<std::size_t>(i)];
                    }
                }
                ImGui::EndListBox();
            }
            if (ImGui::Button("New Material"))
            {
                m_materialEditing = MaterialAsset{};
                m_materialEditing.id = "new_material";
                m_materialEditing.displayName = "New Material";
                m_materialDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Selected Material") && !m_selectedMaterialId.empty())
            {
                if (LevelAssetIO::LoadMaterial(m_selectedMaterialId, &m_materialEditing, nullptr))
                {
                    m_materialDirty = false;
                    m_statusLine = "Loaded material " + m_selectedMaterialId;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected Material") && !m_selectedMaterialId.empty())
            {
                std::string error;
                if (LevelAssetIO::DeleteMaterial(m_selectedMaterialId, &error))
                {
                    m_statusLine = "Deleted material " + m_selectedMaterialId;
                    if (m_materialEditing.id == m_selectedMaterialId)
                    {
                        m_materialEditing = MaterialAsset{};
                        m_materialEditing.id = "new_material";
                        m_materialEditing.displayName = "New Material";
                        m_materialDirty = false;
                    }
                    RefreshLibraries();
                    m_contentNeedsRefresh = true;
                    m_materialCache.clear();
                }
                else
                {
                    m_statusLine = "Delete material failed: " + error;
                }
            }
            if (ImGui::Button("Assign Material To Selected Props") && !m_selectedMaterialId.empty())
            {
                const std::vector<int> selectedProps = SortedUniqueValidSelection(SelectionKind::Prop);
                if (selectedProps.empty())
                {
                    m_statusLine = "Select prop(s) first";
                }
                else
                {
                    PushHistorySnapshot();
                    for (int idx : selectedProps)
                    {
                        if (idx >= 0 && idx < static_cast<int>(m_map.props.size()))
                        {
                            m_map.props[static_cast<std::size_t>(idx)].materialAsset = m_selectedMaterialId;
                        }
                    }
                    std::ostringstream oss;
                    oss << "Assigned material " << m_selectedMaterialId << " to " << selectedProps.size() << " prop(s)";
                    m_statusLine = oss.str();
                }
            }

            ImGui::Separator();
            char materialIdBuffer[128]{};
            std::snprintf(materialIdBuffer, sizeof(materialIdBuffer), "%s", m_materialEditing.id.c_str());
            if (ImGui::InputText("Material Id", materialIdBuffer, sizeof(materialIdBuffer)))
            {
                m_materialEditing.id = materialIdBuffer;
                m_materialDirty = true;
            }
            char materialNameBuffer[128]{};
            std::snprintf(materialNameBuffer, sizeof(materialNameBuffer), "%s", m_materialEditing.displayName.c_str());
            if (ImGui::InputText("Material Name", materialNameBuffer, sizeof(materialNameBuffer)))
            {
                m_materialEditing.displayName = materialNameBuffer;
                m_materialDirty = true;
            }
            int shaderType = static_cast<int>(m_materialEditing.shaderType);
            const char* shaderTypes[] = {"Lit", "Unlit"};
            if (ImGui::Combo("Shader Type", &shaderType, shaderTypes, IM_ARRAYSIZE(shaderTypes)))
            {
                m_materialEditing.shaderType = static_cast<MaterialShaderType>(std::clamp(shaderType, 0, 1));
                m_materialDirty = true;
            }
            if (ImGui::ColorEdit4("Base Color", &m_materialEditing.baseColor.x))
            {
                m_materialDirty = true;
            }
            if (ImGui::DragFloat("Roughness", &m_materialEditing.roughness, 0.01F, 0.0F, 1.0F))
            {
                m_materialDirty = true;
            }
            if (ImGui::DragFloat("Metallic", &m_materialEditing.metallic, 0.01F, 0.0F, 1.0F))
            {
                m_materialDirty = true;
            }
            if (ImGui::DragFloat("Emissive", &m_materialEditing.emissiveStrength, 0.01F, 0.0F, 8.0F))
            {
                m_materialDirty = true;
            }

            char albedoBuffer[256]{};
            std::snprintf(albedoBuffer, sizeof(albedoBuffer), "%s", m_materialEditing.albedoTexture.c_str());
            if (ImGui::InputText("Albedo Texture", albedoBuffer, sizeof(albedoBuffer)))
            {
                m_materialEditing.albedoTexture = albedoBuffer;
                m_materialDirty = true;
            }
            if (ImGui::Button("Save Material"))
            {
                std::string error;
                if (LevelAssetIO::SaveMaterial(m_materialEditing, &error))
                {
                    m_statusLine = "Saved material " + m_materialEditing.id;
                    m_selectedMaterialId = m_materialEditing.id;
                    m_materialCache.clear();
                    m_materialDirty = false;
                    m_contentNeedsRefresh = true;
                    RefreshLibraries();
                }
                else
                {
                    m_statusLine = "Save material failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Material") && !m_selectedMaterialId.empty())
            {
                (void)LevelAssetIO::LoadMaterial(m_selectedMaterialId, &m_materialEditing, nullptr);
                m_materialDirty = false;
            }
            if (m_materialDirty)
            {
                ImGui::TextUnformatted("* Material has unsaved changes");
            }
        }

        if (ImGui::CollapsingHeader("Animation Clip Editor", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Animation Clips");
            if (ImGui::BeginListBox("##animation_library", ImVec2(-1.0F, 90.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_animationLibrary.size()); ++i)
                {
                    const bool selected = m_selectedAnimationIndex == i;
                    if (ImGui::Selectable(m_animationLibrary[static_cast<std::size_t>(i)].c_str(), selected))
                    {
                        m_selectedAnimationIndex = i;
                        m_animationPreviewClip = m_animationLibrary[static_cast<std::size_t>(i)];
                    }
                }
                ImGui::EndListBox();
            }
            if (ImGui::Button("New Clip"))
            {
                m_animationEditing = AnimationClipAsset{};
                m_animationEditing.id = "new_clip";
                m_animationEditing.displayName = "New Clip";
                m_animationEditing.keyframes = {AnimationKeyframe{}};
                m_animationDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Selected Clip") && m_selectedAnimationIndex >= 0 &&
                m_selectedAnimationIndex < static_cast<int>(m_animationLibrary.size()))
            {
                const std::string& clipId = m_animationLibrary[static_cast<std::size_t>(m_selectedAnimationIndex)];
                if (LevelAssetIO::LoadAnimationClip(clipId, &m_animationEditing, nullptr))
                {
                    m_animationPreviewClip = clipId;
                    m_animationDirty = false;
                    m_statusLine = "Loaded animation clip " + clipId;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected Clip") && m_selectedAnimationIndex >= 0 &&
                m_selectedAnimationIndex < static_cast<int>(m_animationLibrary.size()))
            {
                const std::string clipId = m_animationLibrary[static_cast<std::size_t>(m_selectedAnimationIndex)];
                std::string error;
                if (LevelAssetIO::DeleteAnimationClip(clipId, &error))
                {
                    m_statusLine = "Deleted animation clip " + clipId;
                    if (m_animationEditing.id == clipId)
                    {
                        m_animationEditing = AnimationClipAsset{};
                        m_animationEditing.id = "new_clip";
                        m_animationEditing.displayName = "New Clip";
                    }
                    RefreshLibraries();
                    m_contentNeedsRefresh = true;
                    m_animationCache.clear();
                }
                else
                {
                    m_statusLine = "Delete animation failed: " + error;
                }
            }
            if (ImGui::Button("Assign Clip To Selected Props") && !m_animationPreviewClip.empty())
            {
                const std::vector<int> selectedProps = SortedUniqueValidSelection(SelectionKind::Prop);
                if (selectedProps.empty())
                {
                    m_statusLine = "Select prop(s) first";
                }
                else
                {
                    PushHistorySnapshot();
                    for (int idx : selectedProps)
                    {
                        if (idx >= 0 && idx < static_cast<int>(m_map.props.size()))
                        {
                            m_map.props[static_cast<std::size_t>(idx)].animationClip = m_animationPreviewClip;
                        }
                    }
                    std::ostringstream oss;
                    oss << "Assigned animation " << m_animationPreviewClip << " to " << selectedProps.size() << " prop(s)";
                    m_statusLine = oss.str();
                }
            }

            ImGui::Separator();
            char clipIdBuffer[128]{};
            std::snprintf(clipIdBuffer, sizeof(clipIdBuffer), "%s", m_animationEditing.id.c_str());
            if (ImGui::InputText("Clip Id", clipIdBuffer, sizeof(clipIdBuffer)))
            {
                m_animationEditing.id = clipIdBuffer;
                m_animationDirty = true;
            }
            char clipNameBuffer[128]{};
            std::snprintf(clipNameBuffer, sizeof(clipNameBuffer), "%s", m_animationEditing.displayName.c_str());
            if (ImGui::InputText("Clip Name", clipNameBuffer, sizeof(clipNameBuffer)))
            {
                m_animationEditing.displayName = clipNameBuffer;
                m_animationDirty = true;
            }
            if (ImGui::Checkbox("Clip Loop", &m_animationEditing.loop))
            {
                m_animationDirty = true;
            }
            if (ImGui::DragFloat("Clip Speed", &m_animationEditing.speed, 0.02F, 0.01F, 8.0F))
            {
                m_animationDirty = true;
            }

            if (m_animationEditing.keyframes.empty())
            {
                m_animationEditing.keyframes.push_back(AnimationKeyframe{});
                m_animationDirty = true;
            }

            int removeKeyframe = -1;
            ImGui::BeginChild("clip_keyframes", ImVec2(0.0F, 160.0F), true);
            for (int i = 0; i < static_cast<int>(m_animationEditing.keyframes.size()); ++i)
            {
                AnimationKeyframe& key = m_animationEditing.keyframes[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                ImGui::Text("Keyframe %d", i);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    removeKeyframe = i;
                }
                if (ImGui::DragFloat("Time", &key.time, 0.01F, 0.0F, 999.0F, "%.2f"))
                {
                    m_animationDirty = true;
                }
                if (ImGui::DragFloat3("Position", &key.position.x, 0.02F))
                {
                    m_animationDirty = true;
                }
                if (ImGui::DragFloat3("Rotation", &key.rotationEuler.x, 0.5F))
                {
                    m_animationDirty = true;
                }
                if (ImGui::DragFloat3("Scale", &key.scale.x, 0.02F, 0.01F, 10.0F))
                {
                    m_animationDirty = true;
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (removeKeyframe >= 0 && static_cast<int>(m_animationEditing.keyframes.size()) > 1)
            {
                m_animationEditing.keyframes.erase(m_animationEditing.keyframes.begin() + removeKeyframe);
                m_animationDirty = true;
            }

            if (ImGui::Button("Add Keyframe"))
            {
                AnimationKeyframe next;
                if (!m_animationEditing.keyframes.empty())
                {
                    next = m_animationEditing.keyframes.back();
                    next.time += 0.5F;
                }
                m_animationEditing.keyframes.push_back(next);
                m_animationDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Sort by Time"))
            {
                std::sort(
                    m_animationEditing.keyframes.begin(),
                    m_animationEditing.keyframes.end(),
                    [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; }
                );
                m_animationDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Clip"))
            {
                std::sort(
                    m_animationEditing.keyframes.begin(),
                    m_animationEditing.keyframes.end(),
                    [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; }
                );

                std::string error;
                if (LevelAssetIO::SaveAnimationClip(m_animationEditing, &error))
                {
                    m_animationDirty = false;
                    m_animationPreviewClip = m_animationEditing.id;
                    m_statusLine = "Saved animation clip " + m_animationEditing.id;
                    m_contentNeedsRefresh = true;
                    m_animationCache.clear();
                    RefreshLibraries();
                }
                else
                {
                    m_statusLine = "Save animation clip failed: " + error;
                }
            }
            if (m_animationDirty)
            {
                ImGui::TextUnformatted("* Animation clip has unsaved changes");
            }
        }

        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            char envIdBuffer[128]{};
            std::snprintf(envIdBuffer, sizeof(envIdBuffer), "%s", m_environmentEditing.id.c_str());
            if (ImGui::InputText("Environment Id", envIdBuffer, sizeof(envIdBuffer)))
            {
                m_environmentEditing.id = envIdBuffer;
                m_environmentDirty = true;
            }
            if (ImGui::ColorEdit3("Sky Top", &m_environmentEditing.skyTopColor.x)) m_environmentDirty = true;
            if (ImGui::ColorEdit3("Sky Bottom", &m_environmentEditing.skyBottomColor.x)) m_environmentDirty = true;
            ImGui::Checkbox("Clouds Enabled", &m_environmentEditing.cloudsEnabled);
            ImGui::DragFloat("Cloud Coverage", &m_environmentEditing.cloudCoverage, 0.01F, 0.0F, 1.0F);
            ImGui::DragFloat("Cloud Density", &m_environmentEditing.cloudDensity, 0.01F, 0.0F, 1.0F);
            ImGui::DragFloat("Cloud Speed", &m_environmentEditing.cloudSpeed, 0.01F, 0.0F, 8.0F);
            ImGui::DragFloat3("Directional Dir", &m_environmentEditing.directionalLightDirection.x, 0.01F, -1.0F, 1.0F);
            if (ImGui::ColorEdit3("Directional Color", &m_environmentEditing.directionalLightColor.x)) m_environmentDirty = true;
            ImGui::DragFloat("Directional Intensity", &m_environmentEditing.directionalLightIntensity, 0.01F, 0.0F, 8.0F);
            ImGui::Checkbox("Fog Enabled", &m_environmentEditing.fogEnabled);
            if (ImGui::ColorEdit3("Fog Color", &m_environmentEditing.fogColor.x)) m_environmentDirty = true;
            ImGui::DragFloat("Fog Density", &m_environmentEditing.fogDensity, 0.0005F, 0.0F, 0.2F, "%.4f");
            ImGui::DragFloat("Fog Start", &m_environmentEditing.fogStart, 0.1F, 0.0F, 2000.0F);
            ImGui::DragFloat("Fog End", &m_environmentEditing.fogEnd, 0.1F, 0.1F, 3000.0F);
            ImGui::DragInt("Shadow Quality", &m_environmentEditing.shadowQuality, 1.0F, 0, 3);
            ImGui::DragFloat("Shadow Distance", &m_environmentEditing.shadowDistance, 0.5F, 1.0F, 1000.0F);
            ImGui::Checkbox("Tone Mapping", &m_environmentEditing.toneMapping);
            ImGui::DragFloat("Exposure", &m_environmentEditing.exposure, 0.01F, 0.1F, 8.0F);
            ImGui::Checkbox("Bloom", &m_environmentEditing.bloom);

            if (ImGui::Button("Save Environment"))
            {
                std::string error;
                if (LevelAssetIO::SaveEnvironment(m_environmentEditing, &error))
                {
                    m_map.environmentAssetId = m_environmentEditing.id;
                    m_statusLine = "Saved environment " + m_environmentEditing.id;
                    m_environmentDirty = false;
                    m_contentNeedsRefresh = true;
                }
                else
                {
                    m_statusLine = "Save environment failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Environment"))
            {
                if (LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr))
                {
                    m_statusLine = "Loaded environment " + m_map.environmentAssetId;
                }
            }
        }
    }
    ImGui::End();
#else
    (void)outBackToMenu;
    (void)outPlaytestMap;
    (void)outPlaytestMapName;
#endif
}

} // namespace game::editor
