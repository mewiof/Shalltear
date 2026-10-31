#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <span>

namespace Shalltear {

	class Component;
	class SceneObject;
	struct TypeInfo;

	struct SceneIcon {
		Handle<SceneObject> object;
		const TypeInfo* type = nullptr;
		Vec3 worldPosition;
	};

	struct SceneIconPlacement {
		Handle<SceneObject> object;
		const TypeInfo* type = nullptr;
		Vec2 centerPx{};
		f32 sortDepth = 0.0f;
	};

	inline constexpr u32 SCENE_ICON_NONE = ~0u;

	struct GizmoDrawContext {
		Vector<DebugLine>* lines = nullptr;
		Vector<DebugLine>* overlayLines = nullptr;
		Vec3 cameraPosition;
		Quat cameraRotation;
		f32 viewAspect = 1.0f;
		bool selected = false;

		Ray cursorRay;
		bool cursorInView = false;

		Vector<SceneIcon>* icons = nullptr;
	};

	using ComponentGizmoFn = std::function<void(GizmoDrawContext& context, Handle<Component> component)>;

	using SceneGizmoFn = std::function<void(GizmoDrawContext& context)>;

	struct RadiusHandle {
		Vec3 center;
		Quat rotation = Quat::Identity();
		f32 radius = 0.0f;
		StringHash field;
		str label;
		Color32 color;
	};

	using RadiusHandlesFn = std::function<void(Handle<Component> component, Vector<RadiusHandle>& out)>;

	struct ComponentRadius {
		Handle<Component> component;
		RadiusHandle handle;
	};

	enum class SceneHandleKind : u8 {
		Transform,
		Position,
		Rotation,
		Slider
	};

	struct SceneHandle {
		u32 id = 0;
		SceneHandleKind kind = SceneHandleKind::Transform;
		Mat4 parentToWorld = Mat4::Identity();
		Quat parentRotation = Quat::Identity();
		Vec3 position;
		Quat rotation = Quat::Identity();
		Vec3 scale{1.0f, 1.0f, 1.0f};
		Vec3 direction{0.0f, 0.0f, 1.0f};
		f32 distance = 0.0f;
		str label;
		Color32 color;
	};

	using SceneHandlesFn = std::function<void(Vector<SceneHandle>& out)>;
	using SceneHandleWriteFn = std::function<void(const SceneHandle& dragged)>;

	struct OfferedSceneHandle {
		StringHash source;
		SceneHandle handle;
	};

	class SHALLTEAR_EDITOR_API EditorGizmos {
	public:
		static void Initialize();
		static void Terminate();

		static bool RegisterComponentGizmo(StringHash componentTypeName, ComponentGizmoFn gizmo, const void* owner = nullptr);
		static const ComponentGizmoFn* FindComponentGizmo(StringHash componentTypeName);
		static bool RegisterSceneGizmo(StringHash name, SceneGizmoFn gizmo, const void* owner = nullptr);

		static bool RegisterSceneIcon(StringHash componentTypeName, const void* owner = nullptr);

		static bool RegisterRadiusHandles(StringHash componentTypeName, RadiusHandlesFn handles, const void* owner = nullptr);
		static void GatherRadiusHandles(Vector<ComponentRadius>& out);

		static bool RegisterSceneHandles(StringHash name, SceneHandlesFn handles, SceneHandleWriteFn write, const void* owner = nullptr);
		static void GatherSceneHandles(Vector<OfferedSceneHandle>& out);
		static bool WriteSceneHandle(StringHash source, const SceneHandle& dragged);

		static u32 UnregisterOwnedBy(const void* owner);

		// normal must be nonzero
		static void AppendCircle(Vector<DebugLine>& out, const Vec3& center, const Vec3& normal, f32 radius, Color32 color);

		static void AppendOrientedBox(Vector<DebugLine>& out, const Vec3& center, const Quat& rotation,
									  const Vec3& halfExtents, Color32 color);

		static void AppendCapsule(Vector<DebugLine>& out, const Vec3& pointA, const Vec3& pointB, f32 radius, Color32 color);

		static void LayOutSceneIcons(std::span<const SceneIcon> icons, const Mat4& viewProjection,
									 const Vec2& viewportSizePx, f32 sizePx, Vector<SceneIconPlacement>& out);

		// sizePx must match LayOutSceneIcons
		static u32 HitSceneIcon(std::span<const SceneIconPlacement> placements, const Vec2& localPx, f32 sizePx);

		static void AppendCascadeFitLines(Vector<DebugLine>& out, std::span<const CascadeFit> fits, i32 cascadeIndex);

		static void AppendViewFrustumLines(Vector<DebugLine>& out, const Mat4& viewProjection);

		static void DrawSceneGizmos(GizmoDrawContext& context);
	};

} // namespace Shalltear
