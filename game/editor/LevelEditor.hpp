#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/MeshLibrary.hpp"
#include "engine/platform/Input.hpp"
#include "engine/render/Renderer.hpp"
#include "game/editor/LevelAssets.hpp"

namespace game::editor
{
class LevelEditor
{
public:
    enum class Mode
    {
        LoopEditor,
        MapEditor
    };

    enum class GizmoMode
    {
        Translate,
        Rotate,
        Scale
    };

    void Initialize();
    void Enter(Mode mode);

    void Update(
        float deltaSeconds,
        const engine::platform::Input& input,
        bool controlsEnabled,
        int framebufferWidth,
        int framebufferHeight
    );

    void Render(engine::render::Renderer& renderer) const;
    void DrawUi(
        bool* outBackToMenu,
        bool* outPlaytestMap,
        std::string* outPlaytestMapName
    );
    void SetCurrentRenderMode(engine::render::RenderMode mode) { m_currentRenderMode = mode; }
    [[nodiscard]] std::optional<engine::render::RenderMode> ConsumeRequestedRenderMode();
    [[nodiscard]] bool DebugViewEnabled() const { return m_debugView; }
    void SetDebugViewEnabled(bool enabled) { m_debugView = enabled; }

    [[nodiscard]] glm::mat4 BuildViewProjection(float aspectRatio) const;
    [[nodiscard]] Mode CurrentMode() const { return m_mode; }
    [[nodiscard]] const std::string& CurrentMapName() const { return m_map.name; }
    [[nodiscard]] engine::render::EnvironmentSettings CurrentEnvironmentSettings() const;

private:
    enum class SelectionKind
    {
        None,
        LoopElement,
        MapPlacement,
        Prop
    };

    enum class GizmoAxis
    {
        None,
        X,
        Y,
        Z
    };

    struct Selection
    {
        SelectionKind kind = SelectionKind::None;
        int index = -1;
    };

    struct HistoryState
    {
        Mode mode = Mode::MapEditor;
        LoopAsset loop{};
        MapAsset map{};
        Selection selection{};
        std::vector<int> selectedLoopElements;
        std::vector<int> selectedMapPlacements;
        std::vector<int> selectedProps;
        bool propPlacementMode = false;
        int pendingPlacementRotation = 0;
        int paletteLoopIndex = -1;
        PropType selectedPropType = PropType::Rock;
    };

    struct ClipboardState
    {
        SelectionKind kind = SelectionKind::None;
        std::vector<LoopElement> loopElements;
        std::vector<LoopPlacement> mapPlacements;
        std::vector<PropInstance> props;
        bool hasData = false;
        int pasteCount = 0;
    };

    void RefreshLibraries();
    void CreateNewLoop(const std::string& suggestedName = "new_loop");
    void CreateNewMap(const std::string& suggestedName = "new_map");

    [[nodiscard]] glm::vec3 CameraForward() const;
    [[nodiscard]] glm::vec3 CameraUp() const;
    [[nodiscard]] glm::vec3 CameraRight() const;
    void HandleCamera(float deltaSeconds, const engine::platform::Input& input, bool controlsEnabled);
    void HandleEditorHotkeys(const engine::platform::Input& input, bool controlsEnabled);
    void UpdateHoveredTile(const engine::platform::Input& input, int framebufferWidth, int framebufferHeight);

    [[nodiscard]] bool BuildMouseRay(
        const engine::platform::Input& input,
        int framebufferWidth,
        int framebufferHeight,
        glm::vec3* outOrigin,
        glm::vec3* outDirection
    ) const;

    [[nodiscard]] bool RayIntersectGround(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        float groundY,
        glm::vec3* outHit
    ) const;

    [[nodiscard]] Selection PickSelection(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    [[nodiscard]] glm::vec3 SelectionPivot() const;
    [[nodiscard]] bool RayIntersectPlane(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDirection,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal,
        glm::vec3* outHit
    ) const;
    [[nodiscard]] bool StartAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    void UpdateAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    void StopAxisDrag();
    void ApplyGizmoInput(const engine::platform::Input& input, float deltaSeconds);

    [[nodiscard]] glm::ivec2 RotatedFootprint(const LoopAsset& loop, int rotationDegrees) const;
    [[nodiscard]] bool CanPlaceLoopAt(int tileX, int tileY, int rotationDegrees, int ignoredPlacement) const;
    void PlaceLoopAtHoveredTile();
    void PlaceQuickLoopObjectAtHovered(LoopElementType type);
    [[nodiscard]] bool EnsureQuickLoopAsset(LoopElementType type, std::string* outLoopId);
    void RemovePlacementAtHoveredTile();
    void AddPropAtHoveredTile();
    void AddLightAtHovered(LightType type);
    void DeleteCurrentSelection();
    void DuplicateCurrentSelection();
    void CopyCurrentSelection();
    void PasteClipboard();

    void PushHistorySnapshot();
    void Undo();
    void Redo();
    [[nodiscard]] HistoryState CaptureHistoryState() const;
    void RestoreHistoryState(const HistoryState& state);
    void ClearSelections();
    void SelectSingle(const Selection& selection);
    void ToggleSelection(const Selection& selection);
    [[nodiscard]] bool IsSelected(SelectionKind kind, int index) const;
    [[nodiscard]] std::vector<int> SortedUniqueValidSelection(SelectionKind kind) const;

    void AutoComputeLoopBoundsAndFootprint();
    [[nodiscard]] std::vector<std::string> ValidateLoopForUi() const;
    [[nodiscard]] std::string BuildUniqueLoopElementName(const std::string& preferredBaseName) const;
    [[nodiscard]] std::string BuildUniquePropName(const std::string& preferredBaseName) const;
    void RefreshContentBrowser();
    void PlaceImportedAssetAtHovered(const std::string& relativeAssetPath);
    void InstantiatePrefabAtHovered(const std::string& prefabId);
    void SaveSelectedPropsAsPrefab(const std::string& prefabId);
    void ReapplySelectedPrefabInstance();
    [[nodiscard]] const MaterialAsset* GetMaterialCached(const std::string& materialId) const;
    [[nodiscard]] const AnimationClipAsset* GetAnimationClipCached(const std::string& clipId) const;

    [[nodiscard]] glm::vec3 TileCenter(int tileX, int tileY) const;
    [[nodiscard]] std::string SelectedLabel() const;

    Mode m_mode = Mode::MapEditor;
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    Selection m_selection{};

    LoopAsset m_loop{};
    MapAsset m_map{};

    std::vector<std::string> m_loopLibrary;
    std::vector<std::string> m_mapLibrary;
    std::string m_loopSearch;
    std::string m_mapSearch;
    std::string m_loopRenameTarget;
    std::string m_mapRenameTarget;
    std::vector<int> m_selectedLoopElements;
    std::vector<int> m_selectedMapPlacements;
    std::vector<int> m_selectedProps;
    ClipboardState m_clipboard;
    std::vector<HistoryState> m_undoStack;
    std::vector<HistoryState> m_redoStack;
    std::size_t m_historyMaxEntries = 128;
    bool m_historyApplying = false;
    bool m_gizmoEditing = false;
    bool m_axisDragActive = false;
    GizmoAxis m_axisDragAxis = GizmoAxis::None;
    GizmoMode m_axisDragMode = GizmoMode::Translate;
    glm::vec3 m_axisDragPivot{0.0F};
    glm::vec3 m_axisDragDirection{1.0F, 0.0F, 0.0F};
    glm::vec3 m_axisDragPlaneNormal{0.0F, 1.0F, 0.0F};
    glm::vec3 m_axisDragLastVector{1.0F, 0.0F, 0.0F};
    float m_axisDragStartScalar = 0.0F;
    float m_axisDragLastScalar = 0.0F;
    bool m_debugView = true;
    engine::render::RenderMode m_currentRenderMode = engine::render::RenderMode::Wireframe;
    std::optional<engine::render::RenderMode> m_pendingRenderMode;

    int m_selectedLibraryLoop = -1;
    int m_selectedLibraryMap = -1;
    int m_paletteLoopIndex = -1;
    int m_pendingPlacementRotation = 0;
    PropType m_selectedPropType = PropType::Rock;
    int m_selectedLightIndex = -1;

    glm::vec3 m_cameraPosition{0.0F, 24.0F, 24.0F};
    float m_cameraYaw = -2.35F;
    float m_cameraPitch = -0.72F;
    float m_cameraSpeed = 20.0F;
    bool m_topDownView = false;

    bool m_gridSnap = true;
    float m_gridStep = 1.0F;
    bool m_angleSnap = true;
    float m_angleStepDegrees = 15.0F;

    bool m_hoveredTileValid = false;
    glm::ivec2 m_hoveredTile{0, 0};
    glm::vec3 m_hoveredWorld{0.0F};

    bool m_propPlacementMode = false;
    bool m_lightPlacementMode = false;
    LightType m_lightPlacementType = LightType::Point;
    LoopElementType m_quickLoopType = LoopElementType::Wall;
    bool m_autoLitPreview = true;
    std::string m_statusLine = "Editor ready";

    engine::assets::AssetRegistry m_assetRegistry{};
    std::string m_contentDirectory = ".";
    std::vector<engine::assets::AssetEntry> m_contentEntries;
    int m_selectedContentEntry = -1;
    std::string m_contentImportPath;
    std::string m_contentNewFolderName;
    std::string m_contentRenameTarget;
    std::string m_selectedContentPath;
    bool m_contentNeedsRefresh = true;
    std::vector<std::string> m_prefabLibrary;
    int m_selectedPrefabIndex = -1;
    std::string m_prefabNewId = "new_prefab";
    int m_nextPrefabInstanceId = 1;

    std::vector<std::string> m_materialLibrary;
    int m_selectedMaterialIndex = -1;
    std::vector<std::string> m_animationLibrary;
    int m_selectedAnimationIndex = -1;

    MaterialAsset m_materialEditing{};
    bool m_materialDirty = false;
    std::string m_selectedMaterialId;

    AnimationClipAsset m_animationEditing{};
    bool m_animationDirty = false;

    EnvironmentAsset m_environmentEditing{};
    bool m_environmentDirty = false;

    bool m_animationPreviewPlaying = false;
    float m_animationPreviewTime = 0.0F;
    std::string m_animationPreviewClip;
    mutable std::unordered_map<std::string, MaterialAsset> m_materialCache;
    mutable std::unordered_map<std::string, AnimationClipAsset> m_animationCache;
    mutable engine::assets::MeshLibrary m_meshLibrary;
};
} // namespace game::editor
