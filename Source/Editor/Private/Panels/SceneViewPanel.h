#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorGizmoOps.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorInputOps.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorViewportOps.h"

#include "EditorGrid.h"
#include "ViewportHost.h"

#include <algorithm>
#include <string_view>

namespace Shalltear {

	class SceneObject;

	class SceneViewPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(SceneViewPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Scene View")
		SLT_REFLECT_FIELD(cameraPosition)
		SLT_REFLECT_FIELD(cameraYawDeg)
		SLT_REFLECT_FIELD(cameraPitchDeg)
		SLT_REFLECT_FIELD(flySpeed)
		SLT_REFLECT_FIELD(mode2D)
		SLT_REFLECT_FIELD(parkedPosition)
		SLT_REFLECT_FIELD(parkedYawDeg)
		SLT_REFLECT_FIELD(parkedPitchDeg)
		SLT_REFLECT_FIELD(parkedOrthographic)
		SLT_REFLECT_FIELD(orthographic)
		SLT_REFLECT_FIELD(orthographicSize)
		SLT_REFLECT_FIELD(showGrid)
		SLT_REFLECT_FIELD(showGizmos)
		SLT_REFLECT_FIELD(showEffects)
		SLT_REFLECT_FIELD(alwaysRefresh)
		SLT_REFLECT_FIELD(showPostProcessing)
		SLT_REFLECT_ARRAY(hiddenEffects)
		SLT_REFLECT_FIELD(shadingMode)
		SLT_REFLECT_FIELD(showCascadeFrustum)
		SLT_REFLECT_FIELD(cascadeFrustumIndex)
		SLT_REFLECT_FIELD(atlasOverlay)
		SLT_REFLECT_FIELD(gizmoMode)
		SLT_REFLECT_FIELD(gizmoSpace)
		SLT_REFLECT_FIELD(snapTranslate)
		SLT_REFLECT_FIELD(snapRotateDeg)
		SLT_REFLECT_FIELD(snapScale)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		bool WantsPlayModeTint() const override { return false; }

		SceneViewPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnSubmitView(f32 deltaSeconds) override;
		void OnOpen() override;
		void OnClose() override;
		bool OnCommand(PanelCommand command) override;

		static constexpr f32 DEFAULT_CAMERA_YAW_DEG = 0.0f;
		static constexpr f32 DEFAULT_CAMERA_PITCH_DEG = 15.0f;

		Vec3 cameraPosition{0.0f, 3.0f, -8.0f};
		f32 cameraYawDeg = DEFAULT_CAMERA_YAW_DEG;
		f32 cameraPitchDeg = DEFAULT_CAMERA_PITCH_DEG;
		f32 flySpeed = 6.0f;
		bool mode2D = false;
		Vec3 parkedPosition;
		f32 parkedYawDeg = DEFAULT_CAMERA_YAW_DEG;
		f32 parkedPitchDeg = DEFAULT_CAMERA_PITCH_DEG;
		bool parkedOrthographic = false;
		bool orthographic = false;
		// half height
		f32 orthographicSize = EditorViewportOps::EDITOR_ORTHOGRAPHIC_SIZE;
		bool showGrid = true;
		bool showGizmos = true;
		bool showEffects = true;
		bool alwaysRefresh = false;
		bool showPostProcessing = true;
		Vector<str> hiddenEffects{GetMemoryResource(MemTag::Editor)};

		bool ShowsEffect(std::string_view label) const {
			return showEffects && std::find(hiddenEffects.begin(), hiddenEffects.end(), label) == hiddenEffects.end();
		}
		i32 shadingMode = static_cast<i32>(ViewFill::Solid);
		bool showCascadeFrustum = false;
		i32 cascadeFrustumIndex = -1;
		i32 atlasOverlay = static_cast<i32>(DebugOverlay::None);
		i32 gizmoMode = static_cast<i32>(GizmoMode::Translate);
		i32 gizmoSpace = static_cast<i32>(GizmoSpace::Local);
		f32 snapTranslate = 1.0f;
		f32 snapRotateDeg = 15.0f;
		f32 snapScale = 0.1f;

		ViewTargetId GetViewTarget() const { return _viewport.GetTarget(); }
		ViewFill GetShadingFill() const;
		DebugView GetDebugView() const;
		void FrameSelection();
		void ToggleProjection();
		void SetMode2D(bool enabled);
		bool RotationIsLocked() const { return mode2D || _rotationLocked; }
	private:
		void Follow(Handle<SceneObject> object);
		void StopFollowing();
		Handle<SceneObject> FollowTarget() const;
		bool _freezeShadowFit = false;
		EditorViewportOps::Viewpoint _frozenFit{};

		void OnFollowRequested(const EditorFollowRequestedEvent& event);
		void OnWorldRemapped(const EditorWorldRemappedEvent& event);
		void RefreshFollowChip(UI::Context& context);

		void BuildOrientationOverlay(UI::Context& context);
		void UpdateComponentOverlays(UI::Context& context);
		void RefreshOrientationVisibility(UI::Context& context);
		void FillOrientationBar(UI::Context& context);
		void LayOutOrientation(UI::Context& context);
		void PaintOrientation(UI::Context& context, UI::ElementHandle canvas, UI::PaintScope& paint);
		void OnOrientationClick(UI::Context& context);
		void OpenOrientationMenu(UI::Context& context);
		void OpenEffectsMenu(UI::Context& context);
		void OpenGizmosMenu(UI::Context& context);
		void ZoomOrthographic(f32 wheel);
		void SnapViewToAxis(const Vec3& axis);
		void ResetViewAngle();
		Vec3 PivotPoint() const;
		i32 ClampedShadingMode() const;
		u32 CascadeFitRow() const;
		DebugOverlay ClampedAtlasOverlay() const;

		void BuildToolbar(UI::Context& context, UI::ElementHandle root);
		void FillToolbar(UI::Context& context);
		void RefreshToolbar(UI::Context& context);

		void UpdateCamera(UI::Context& context, f32 deltaSeconds);
		void BuildDebugLines();
		void LayOutIcons(UI::Context& context, const Vec2& sizePx);
		void PaintIcons(UI::PaintScope& paint, const UI::Rect& area);

		void OnViewportPress(UI::Context& context);
		void ApplyPick(Handle<SceneObject> picked, bool toggle);
		void TakePickAnswers();
		void OnViewportDrag(UI::Context& context, Vec2 contextPx);
		void OnViewportRelease();
		void OnViewportDrop(UI::Context& context, const UI::DragPayload& payload, Vec2 contextPx);

		void RefreshGizmoTargets();
		std::string_view DragLabel() const;
		Quat GizmoOrientationFor(const SceneObject& target) const;
		// sizePx must be the viewport's (not the window's)
		bool BuildGizmoDesc(const Vec3& origin, const Quat& orientation, const Vec2& sizePx,
							EditorGizmoOps::GizmoDesc& out) const;
		EditorViewportOps::Viewpoint CurrentViewpoint() const;
		Ray RayAt(UI::Context& context, Vec2 contextPx) const;
		Mat4 ViewProjection() const;
		static bool SnapHeld(UI::Context& context);

		bool VertexSnapHeld() const;
		void UpdateVertexAnchor(UI::Context& context, const Vec2& sizePx);
		void UpdateVertexDrag(UI::Context& context, Vec2 contextPx);
		void UpdateRadii(UI::Context& context, const Vec2& sizePx);
		void UpdateRadiusDrag(UI::Context& context, Vec2 contextPx);
		void UpdateSceneHandles(UI::Context& context, const Vec2& sizePx);
		bool SceneHandleDesc(const SceneHandle& handle, GizmoMode mode, const Vec2& sizePx, EditorGizmoOps::GizmoDesc& out) const;
		void UpdateSceneHandleDrag(UI::Context& context, Vec2 contextPx);
		void PushDragCommands();

		ViewportHost _viewport;
		UI::ElementHandle _toolbar;
		UI::ElementHandle _statusLabel;
		bool _toolbarDirty = false;

		Vector<DebugLine> _lines{GetMemoryResource(MemTag::Editor)};
		Vector<DebugLine> _overlayLines{GetMemoryResource(MemTag::Editor)};
		Vector<ViewOutline> _outlines{GetMemoryResource(MemTag::Editor)};
		struct PendingPick {
			u64 tag = 0;
			bool toggle = false;
		};
		ViewPick _pick;
		bool _pickAsked = false;
		u64 _nextPickTag = 1;
		Vector<PendingPick> _pendingPicks{GetMemoryResource(MemTag::Editor)};

		Vector<SceneIcon> _sceneIcons{GetMemoryResource(MemTag::Editor)};
		Vector<SceneIconPlacement> _iconPlacements{GetMemoryResource(MemTag::Editor)};
		f32 _iconSizePx = 0.0f;
		// must outlive the SubmitView call
		EditorGridPass _gridPass;

		Vector<Handle<RenderPass>> _passRows{GetMemoryResource(MemTag::Editor)};
		f64 _refreshClock = 0.0;
		Vector<StringHash> _hiddenRenderers{GetMemoryResource(MemTag::Editor)};

		EditorGizmoOps::DragState _drag;
		Vector<Handle<SceneObject>> _gizmoTargets{GetMemoryResource(MemTag::Editor)};
		Vector<EditorGizmoOps::DragSnapshot> _dragStarts{GetMemoryResource(MemTag::Editor)};
		Vector<EditorGizmoOps::DragOutcome> _dragOutcomes{GetMemoryResource(MemTag::Editor)};
		bool _dragGroupOpen = false;

		Vec3 _vertexAnchor;
		Vec3 _vertexLive;
		bool _vertexAnchorValid = false;
		bool _vertexDrag = false;
		Vector<Handle<SceneObject>> _vertexExcluded{GetMemoryResource(MemTag::Editor)};

		Vector<ComponentRadius> _radii{GetMemoryResource(MemTag::Editor)};
		Vector<EditorGizmoOps::RadiusDot> _radiusDots{GetMemoryResource(MemTag::Editor)};
		Vector<u32> _radiusDotOwners{GetMemoryResource(MemTag::Editor)};
		i32 _radiusHover = -1;
		bool _radiusDragging = false;
		EditorGizmoOps::RadiusDrag _radiusDrag;
		Handle<Component> _radiusComponent;
		StringHash _radiusField;
		str _radiusLabel;
		f32 _radiusStart = 0.0f;

		Vector<OfferedSceneHandle> _sceneHandles{GetMemoryResource(MemTag::Editor)};
		i32 _sceneHover = -1;
		EditorGizmoOps::GizmoHandle _sceneHoverPart = EditorGizmoOps::GizmoHandle::None;
		bool _sceneDragging = false;
		StringHash _sceneSource;
		SceneHandle _sceneStart;
		GizmoMode _sceneMode = GizmoMode::Translate;
		EditorGizmoOps::DragState _sceneDrag;
		EditorGizmoOps::RadiusDrag _sceneSlider;

		InputActionMap _viewportKeys;
		ActionId _vertexSnapAction = INVALID_ACTION_ID;
		bool _pointerRouteOpen = false;

		u64 _followTargetId = 0;
		f32 _followDistance = 0.0f;
		UI::ElementHandle _followChip;
		u64 _followChipFor = 0;
		str _followChipName;

		UI::ElementHandle _componentOverlays;
		u64 _componentOverlaysKey = 0;

		UI::ElementHandle _orientation;
		UI::ElementHandle _orientationCanvas;
		UI::ElementHandle _orientationBar;
		UI::ElementHandle _orientationLabels[3];
		EditorViewportOps::OrientationKnob _orientationKnobs[EditorViewportOps::ORIENTATION_KNOB_COUNT];
		f32 _orientationSidePx = 0.0f;
		u32 _orientationHover = EditorViewportOps::ORIENTATION_KNOB_NONE;
		bool _orientationBarDirty = false;
		bool _rotationLocked = false;
		f32 _pivotDistance = 10.0f;

		EditorGizmoOps::GizmoHandle _hover = EditorGizmoOps::GizmoHandle::None;
		bool _looking = false;
		u32 _pointerClaim = 0;
		f32 _statusAccum = 1.0e6f;
	};

} // namespace Shalltear
