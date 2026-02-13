#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <array>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/MeshLibrary.hpp"
#include "engine/fx/FxSystem.hpp"
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

    enum class MaterialLabViewMode
    {
        Off,
        Overlay,
        Dedicated
    };

    enum class UiWorkspace
    {
        All,
        Mesh,
        Map,
        Lighting,
        FxEnv
    };

    enum class LayoutRegion
    {
        Top = 0,
        Left = 1,
        Center = 2,
        Right = 3,
        BottomLeft = 4,
        BottomRight = 5,
    };

    enum class DockPanel
    {
        ToolSettings = 0,
        LoopLibrary = 1,
        LoopEditor = 2,
        MapLibrary = 3,
        LoopPalette = 4,
        Prefabs = 5,
        MapEditor = 6,
        Inspector = 7,
        ContentBrowser = 8,
        MaterialsEnvironment = 9,
        FxEditor = 10,
        Outliner = 11,
        Count = 12,
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
    [[nodiscard]] glm::vec3 CameraPosition() const { return m_cameraPosition; }
    [[nodiscard]] bool EditorLightingEnabled() const { return m_materialLabLightingEnabled; }
    [[nodiscard]] std::string SceneDump() const;

    [[nodiscard]] glm::mat4 BuildViewProjection(float aspectRatio) const;
    [[nodiscard]] Mode CurrentMode() const { return m_mode; }
    [[nodiscard]] const std::string& CurrentMapName() const { return m_map.name; }
    [[nodiscard]] engine::render::EnvironmentSettings CurrentEnvironmentSettings() const;
    void QueueExternalDroppedFiles(const std::vector<std::string>& absolutePaths);

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

    struct MeshModelVertex
    {
        glm::vec3 position{0.0F};
        bool deleted = false;
    };

    struct MeshModelFace
    {
        std::array<int, 4> indices{0, 0, 0, 0};
        int vertexCount = 4;
        bool deleted = false;

        MeshModelFace() = default;
        MeshModelFace(const std::array<int, 4>& inIndices, bool inDeleted = false, int inVertexCount = 4)
            : indices(inIndices)
            , vertexCount(std::clamp(inVertexCount, 3, 4))
            , deleted(inDeleted)
        {
        }
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
        std::vector<MeshModelVertex> meshModelVertices;
        std::vector<MeshModelFace> meshModelFaces;
        int meshModelSelectedFace = -1;
        int meshModelSelectedVertex = -1;
        int meshModelSelectedEdge = -1;
        std::vector<int> meshModelFaceSelection;
        std::vector<int> meshModelVertexSelection;
        std::vector<int> meshModelLoopSelectionEdges;
        std::vector<int> meshModelRingSelectionEdges;
        glm::vec3 meshModelPosition{0.0F, 1.1F, 0.0F};
        glm::vec3 meshModelScale{1.0F, 1.0F, 1.0F};
        std::string meshActiveObjectName{"mesh_object"};
        bool meshModelSceneEditEnabled = true;
        int meshEditMode = 0;
        int meshObjectMode = 1;
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

    enum class ModalConstraintMode
    {
        None,
        Axis,
        Plane
    };

    enum class TransformOrientation
    {
        Global,
        Local
    };

    enum class NumericInputMode
    {
        Absolute,
        Multiply,
        Divide
    };

    enum class SelectionCombineMode
    {
        Replace,
        Add,
        Subtract,
        Intersect
    };

    enum class SnapType
    {
        Increment = 0,
        Vertex = 1,
        Edge = 2,
        Face = 3,
    };

    struct SceneLoopElementTransform
    {
        int index = -1;
        glm::vec3 position{0.0F};
        glm::vec3 halfExtents{1.0F};
        glm::vec3 rotationEuler{0.0F};
    };

    struct ScenePropTransform
    {
        int index = -1;
        glm::vec3 position{0.0F};
        glm::vec3 halfExtents{1.0F};
        glm::vec3 rotationEuler{0.0F};
    };

    struct ScenePlacementTransform
    {
        int index = -1;
        int tileX = 0;
        int tileY = 0;
        int rotationDegrees = 0;
    };

    struct MeshVertexTransform
    {
        int index = -1;
        glm::vec3 position{0.0F};
    };

    struct ModalTransformState
    {
        bool active = false;
        bool meshSelection = false;
        GizmoMode tool = GizmoMode::Translate;
        ModalConstraintMode constraintMode = ModalConstraintMode::None;
        GizmoAxis constraintAxis = GizmoAxis::None;
        TransformOrientation orientation = TransformOrientation::Global;
        glm::vec2 startMouse{0.0F};
        glm::vec2 lastMouse{0.0F};
        glm::vec3 pivotWorld{0.0F};
        glm::vec3 viewRight{1.0F, 0.0F, 0.0F};
        glm::vec3 viewUp{0.0F, 1.0F, 0.0F};
        std::string numericInput;
        bool numericNegative = false;
        NumericInputMode numericMode = NumericInputMode::Absolute;
        float previewTranslateMagnitude = 0.0F;
        float previewRotateDegrees = 0.0F;
        float previewScaleFactor = 1.0F;
        glm::vec3 previewWorldDelta{0.0F};
        std::vector<SceneLoopElementTransform> loopElements;
        std::vector<ScenePropTransform> props;
        std::vector<ScenePlacementTransform> placements;
        std::vector<MeshVertexTransform> meshVertices;
    };

    struct BoxSelectionState
    {
        bool active = false;
        bool dragging = false;
        glm::vec2 start{0.0F};
        glm::vec2 end{0.0F};
    };

    struct LassoSelectionState
    {
        bool active = false;
        std::vector<glm::vec2> points;
    };

    void RefreshLibraries();
    void CreateNewLoop(const std::string& suggestedName = "new_loop");
    void CreateNewMap(const std::string& suggestedName = "new_map");

    [[nodiscard]] glm::vec3 CameraForward() const;
    [[nodiscard]] glm::vec3 CameraUp() const;
    [[nodiscard]] glm::vec3 CameraRight() const;
    [[nodiscard]] glm::vec3 CameraFocusPivot() const;
    void FocusCameraOnSelection();
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
    [[nodiscard]] bool HasSceneSelection() const;
    [[nodiscard]] bool HasMeshSelection() const;
    void BeginModalTransform(GizmoMode tool, const engine::platform::Input& input, bool pushHistory = true);
    void UpdateModalTransform(const engine::platform::Input& input, float deltaSeconds);
    void ConfirmModalTransform();
    void CancelModalTransform();
    void UpdateModalConstraint(const engine::platform::Input& input);
    [[nodiscard]] glm::vec3 ApplyModalAxisConstraint(const glm::vec3& worldDelta) const;
    [[nodiscard]] glm::vec3 ModalRotationAxis() const;
    [[nodiscard]] glm::vec3 ModalConstraintAxisDirection() const;
    [[nodiscard]] float ApplyModalNumericMode(float baseValue, const std::optional<float>& numericOverride) const;
    [[nodiscard]] std::optional<float> ConsumeModalNumericInput(const engine::platform::Input& input);
    void ApplyBoxSelection(SelectionCombineMode mode);
    void ApplyLassoSelection(SelectionCombineMode mode);
    [[nodiscard]] bool WorldToScreenPoint(const glm::vec3& world, glm::vec2* outScreen) const;
    [[nodiscard]] std::vector<int> CollectModalMeshVertexSelection() const;

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
    void ClearContentPreviewCache();
    void TouchContentPreviewLru(const std::string& key);
    void EnforceContentPreviewLru();
    unsigned int GetOrCreateMeshSurfaceAlbedoTexture(
        const std::string& meshPath,
        std::size_t surfaceIndex,
        const engine::assets::MeshSurfaceData& surface
    ) const;
    void ClearMeshAlbedoTextureCache() const;
    void PlaceImportedAssetAtHovered(const std::string& relativeAssetPath);
    void InstantiatePrefabAtHovered(const std::string& prefabId);
    void SaveSelectedPropsAsPrefab(const std::string& prefabId);
    void ReapplySelectedPrefabInstance();
    [[nodiscard]] const MaterialAsset* GetMaterialCached(const std::string& materialId) const;
    [[nodiscard]] const AnimationClipAsset* GetAnimationClipCached(const std::string& clipId) const;
    void ResetMeshModelerToCube();
    void ResetMeshModelerToSquare();
    void ResetMeshModelerToCircle(int segments, float radius);
    void ResetMeshModelerToSphere(int latSegments, int lonSegments, float radius);
    void ResetMeshModelerToCapsule(int segments, int hemiRings, int cylinderRings, float radius, float height);
    void ResetMeshModelerCommonState();
    void MeshModelerSubdivideFace(int faceIndex);
    void MeshModelerCutFace(int faceIndex, bool verticalCut);
    void MeshModelerExtrudeFace(int faceIndex, float distance);
    void MeshModelerExtrudeEdge(int edgeIndex, float distance);
    void MeshModelerExtrudeActiveEdges(float distance);
    void MeshModelerBevelEdge(int edgeIndex, float distance, int segments);
    void MeshModelerBevelActiveEdges(float distance, int segments);
    void MeshModelerLoopCutEdge(int edgeIndex, float ratio);
    void MeshModelerMergeVertices(int keepVertexIndex, int removeVertexIndex);
    void MeshModelerSplitSelectedVertex();
    void MeshModelerDissolveSelectedEdge();
    void MeshModelerBridgeEdges(int edgeIndexA, int edgeIndexB);
    void MeshModelerDeleteFace(int faceIndex);
    void MeshModelerDissolveFace(int faceIndex);
    void MeshModelerMoveVertex(int vertexIndex, const glm::vec3& delta);
    bool ExportMeshModelerObj(const std::string& assetName, std::string* outRelativePath, std::string* outError) const;
    void RenderMeshModeler(engine::render::Renderer& renderer) const;
    [[nodiscard]] std::vector<std::array<int, 2>> BuildMeshModelEdges() const;
    [[nodiscard]] std::vector<int> CollectMeshModelActiveEdges() const;
    [[nodiscard]] glm::vec3 MeshModelSelectionPivot() const;
    void MoveMeshSelection(const glm::vec3& delta);
    [[nodiscard]] bool PickMeshModelInScene(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    [[nodiscard]] bool RaycastMeshModel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, int* outFaceIndex, glm::vec3* outHitPoint) const;
    [[nodiscard]] bool BuildActiveMeshLocalBasis(glm::mat3* outBasis) const;
    void UpdateMeshHover(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    [[nodiscard]] bool BuildKnifePreviewSegments(
        const glm::vec3& lineStartWorld,
        const glm::vec3& lineEndWorld,
        std::vector<std::pair<glm::vec3, glm::vec3>>* outSegments
    ) const;
    void MeshModelerSelectEdgeLoop(int edgeIndex);
    void MeshModelerSelectEdgeRing(int edgeIndex);
    [[nodiscard]] bool StartMeshAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    void UpdateMeshAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    void StopMeshAxisDrag();
    [[nodiscard]] bool ComputeMeshBatchEdgeGizmo(
        glm::vec3* outPivot,
        glm::vec3* outDirection,
        glm::vec3* outPlaneNormal,
        float* outAxisLength
    ) const;
    [[nodiscard]] bool StartMeshBatchEdgeDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    void UpdateMeshBatchEdgeDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    void StopMeshBatchEdgeDrag();
    [[nodiscard]] bool HandleMeshKnifeClick(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
    void CleanupMeshModelTopology();

    [[nodiscard]] glm::vec3 TileCenter(int tileX, int tileY) const;
    [[nodiscard]] std::string SelectedLabel() const;
    void ResetEditorLayout();
    void LoadEditorLayout();
    void SaveEditorLayout() const;
    [[nodiscard]] std::filesystem::path EditorLayoutPath() const;
    [[nodiscard]] const char* DockPanelTitle(DockPanel panel) const;
    void SetMeshObjectEditMode(bool editMode, const char* reason);
    void ToggleMeshObjectEditMode(const char* reason);
    void QueueMeshPrimitiveSpawn(int primitiveTypeRaw);
    void SpawnQueuedMeshPrimitive(const engine::platform::Input& input);
    void SpawnMeshPrimitiveNow(int primitiveTypeRaw, const glm::vec3& position, const char* sourceLabel);
    [[nodiscard]] glm::vec3 ComputeMeshSpawnPosition(int spawnModeRaw, const engine::platform::Input& input) const;
    void UpdateMeshCursorFromMouse(const engine::platform::Input& input, int framebufferWidth, int framebufferHeight);
    [[nodiscard]] const char* MeshSpawnModeToText(int spawnModeRaw) const;
    [[nodiscard]] const char* MeshPrimitiveToText(int primitiveTypeRaw) const;
    void TryActivateMeshFromCurrentSelection();
    void BuildMeshModelFromProp(const PropInstance& prop);

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
    bool m_dockLayoutBuilt = false;
    UiWorkspace m_uiWorkspace = UiWorkspace::All;

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
    glm::vec3 m_cameraOrbitTarget{0.0F, 0.0F, 0.0F};
    bool m_cameraOrbitTargetValid = false;

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
    bool m_contentBrowserHovered = false;
    std::vector<std::string> m_pendingExternalDrops;
    struct ContentPreviewTexture
    {
        unsigned int textureId = 0;
        int width = 0;
        int height = 0;
        bool failed = false;
    };
    std::unordered_map<std::string, ContentPreviewTexture> m_contentPreviews;
    std::vector<std::string> m_contentPreviewLru;
    std::size_t m_contentPreviewLruCapacity = 192;
    mutable std::unordered_map<std::string, unsigned int> m_meshAlbedoTextures;
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
    engine::fx::FxSystem m_fxPreviewSystem{};
    std::vector<std::string> m_fxLibrary;
    int m_selectedFxIndex = -1;
    engine::fx::FxAsset m_fxEditing{};
    bool m_fxDirty = false;
    int m_selectedFxEmitterIndex = -1;

    std::vector<MeshModelVertex> m_meshModelVertices;
    std::vector<MeshModelFace> m_meshModelFaces;
    int m_meshModelSelectedFace = -1;
    int m_meshModelSelectedVertex = -1;
    std::vector<int> m_meshModelFaceSelection;
    std::vector<int> m_meshModelVertexSelection;
    int m_meshModelHoveredFace = -1;
    int m_meshModelHoveredVertex = -1;
    float m_meshModelExtrudeDistance = 0.6F;
    glm::vec3 m_meshModelVertexDelta{0.0F};
    glm::vec3 m_meshModelPosition{0.0F, 1.1F, 0.0F};
    glm::vec3 m_meshModelScale{1.0F, 1.0F, 1.0F};
    std::string m_meshModelAssetName = "generated_mesh";
    int m_meshPrimitiveCircleSegments = 24;
    int m_meshPrimitiveSphereLatSegments = 18;
    int m_meshPrimitiveSphereLonSegments = 32;
    int m_meshPrimitiveCapsuleSegments = 24;
    int m_meshPrimitiveCapsuleHemiRings = 8;
    int m_meshPrimitiveCapsuleCylinderRings = 4;
    float m_meshPrimitiveRadius = 0.75F;
    float m_meshPrimitiveHeight = 1.8F;
    int m_meshModelSelectedEdge = -1;
    int m_meshModelHoveredEdge = -1;
    std::vector<int> m_meshModelLoopSelectionEdges;
    std::vector<int> m_meshModelRingSelectionEdges;
    enum class MeshEditMode
    {
        Face,
        Edge,
        Vertex
    };
    MeshEditMode m_meshEditMode = MeshEditMode::Face;
    enum class MeshObjectMode
    {
        Object,
        Edit
    };
    MeshObjectMode m_meshObjectMode = MeshObjectMode::Edit;
    enum class MeshSpawnMode
    {
        Cursor = 0,
        CameraRaycast = 1,
        ClickPlace = 2
    };
    enum class MeshPrimitiveType
    {
        None = 0,
        Cube = 1,
        Sphere = 2,
        Plane = 3,
        Cylinder = 4,
        Cone = 5,
        Capsule = 6
    };
    MeshSpawnMode m_meshSpawnMode = MeshSpawnMode::Cursor;
    MeshPrimitiveType m_pendingMeshPrimitive = MeshPrimitiveType::None;
    MeshPrimitiveType m_clickPlacePrimitive = MeshPrimitiveType::None;
    bool m_meshClickPlacementPending = false;
    glm::vec3 m_meshCursorPosition{0.0F, 1.1F, 0.0F};
    glm::vec3 m_meshClickPreviewPosition{0.0F, 1.1F, 0.0F};
    std::string m_meshActiveObjectName = "mesh_object";
    int m_meshObjectCounter = 1;
    bool m_meshModelSceneEditEnabled = true;
    bool m_meshModelShowGizmo = true;
    enum class MeshBatchEdgeOperation
    {
        Extrude,
        Bevel
    };
    MeshBatchEdgeOperation m_meshModelBatchOperation = MeshBatchEdgeOperation::Bevel;
    bool m_meshModelBatchGizmoEnabled = true;
    bool m_meshModelBatchDragActive = false;
    float m_meshModelBatchPreviewDistance = 0.15F;
    glm::vec3 m_meshModelBatchDragPivot{0.0F};
    glm::vec3 m_meshModelBatchDragDirection{0.0F, 1.0F, 0.0F};
    glm::vec3 m_meshModelBatchDragPlaneNormal{1.0F, 0.0F, 0.0F};
    float m_meshModelBatchDragStartScalar = 0.0F;
    bool m_meshModelAxisDragActive = false;
    GizmoAxis m_meshModelAxisDragAxis = GizmoAxis::None;
    glm::vec3 m_meshModelAxisDragPivot{0.0F};
    glm::vec3 m_meshModelAxisDragDirection{1.0F, 0.0F, 0.0F};
    glm::vec3 m_meshModelAxisDragPlaneNormal{0.0F, 1.0F, 0.0F};
    float m_meshModelAxisDragStartScalar = 0.0F;
    float m_meshModelAxisDragLastScalar = 0.0F;
    float m_meshModelBevelDistance = 0.15F;
    int m_meshModelBevelSegments = 2;
    float m_meshModelBevelProfile = 1.0F;
    bool m_meshModelBevelUseMiter = true;
    float m_meshModelLoopCutRatio = 0.5F;
    int m_meshModelBridgeEdgeA = -1;
    int m_meshModelBridgeEdgeB = -1;
    int m_meshModelMergeKeepVertex = -1;
    int m_meshModelMergeRemoveVertex = -1;
    bool m_meshModelKnifeEnabled = false;
    bool m_meshModelLoopCutToolEnabled = false;
    bool m_meshModelKnifeHasFirstPoint = false;
    int m_meshModelKnifeFaceIndex = -1;
    glm::vec3 m_meshModelKnifeFirstPointLocal{0.0F};
    glm::vec3 m_meshModelKnifeFirstPointWorld{0.0F};
    bool m_meshModelKnifePreviewValid = false;
    glm::vec3 m_meshModelKnifePreviewWorld{0.0F};
    std::vector<std::pair<glm::vec3, glm::vec3>> m_meshModelKnifePreviewSegments;

    MaterialLabViewMode m_materialLabViewMode = MaterialLabViewMode::Off;
    bool m_materialLabLightingEnabled = true;
    bool m_materialLabDirectionalLightEnabled = true;
    bool m_materialLabPointLightsEnabled = true;
    bool m_materialLabAutoRotate = true;
    bool m_materialLabForceFilled = true;
    bool m_materialLabBackdropEnabled = true;
    float m_materialLabDistance = 4.6F;
    float m_materialLabHeight = -0.5F;
    float m_materialLabSphereRadius = 0.75F;
    float m_materialLabAutoRotateSpeed = 26.0F;
    float m_materialLabManualYaw = 0.0F;
    float m_materialLabDirectionalIntensity = 1.2F;
    float m_materialLabPointIntensity = 5.5F;
    float m_materialLabPointRange = 12.0F;
    float m_materialLabElapsed = 0.0F;

    bool m_sceneViewportHovered = false;
    bool m_sceneViewportFocused = false;
    glm::vec2 m_sceneViewportRectMin{0.0F};
    glm::vec2 m_sceneViewportRectMax{0.0F};
    float m_layoutTopHeightRatio = 0.13F;
    float m_layoutBottomHeightRatio = 0.24F;
    float m_layoutLeftWidthRatio = 0.23F;
    float m_layoutRightWidthRatio = 0.26F;
    float m_layoutBottomSplitRatio = 0.52F;
    bool m_layoutNeedsSave = false;
    std::array<LayoutRegion, static_cast<std::size_t>(DockPanel::Count)> m_panelRegion{};
    std::array<bool, static_cast<std::size_t>(DockPanel::Count)> m_panelVisible{};
    ModalTransformState m_modalTransform{};
    BoxSelectionState m_boxSelection{};
    LassoSelectionState m_lassoSelection{};
    bool m_blenderLayoutPreset = true;
    bool m_showShortcutGuide = false;
    bool m_showEditorManual = false;
    bool m_showToolbar = true;
    bool m_showStatusBar = true;
    TransformOrientation m_transformOrientationUi = TransformOrientation::Global;
    SnapType m_snapType = SnapType::Increment;
    int m_lastFramebufferWidth = 1;
    int m_lastFramebufferHeight = 1;
    SelectionCombineMode m_selectionCombineMode = SelectionCombineMode::Replace;
};
} // namespace game::editor
