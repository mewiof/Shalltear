#include "Shalltear/Editor/EditorGizmos.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"

#include <algorithm>
#include <cmath>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;

		Color32 CameraWire() { return EditorTheme::GetColor32(ColorToken::GizmoCameraWire); }
		Color32 LightWire() { return EditorTheme::GetColor32(ColorToken::GizmoLightWire); }

		struct GizmoEntry {
			ComponentGizmoFn gizmo;
			const void* owner = nullptr;
		};

		struct SceneGizmoEntry {
			SceneGizmoFn gizmo;
			const void* owner = nullptr;
		};

		struct RadiusEntry {
			RadiusHandlesFn handles;
			const void* owner = nullptr;
		};

		struct SceneHandleEntry {
			SceneHandlesFn handles;
			SceneHandleWriteFn write;
			const void* owner = nullptr;
		};

		struct GizmoState {
			HashMap<StringHash, GizmoEntry> gizmos{GetMemoryResource(MemTag::Editor)};
			HashMap<StringHash, SceneGizmoEntry> sceneGizmos{GetMemoryResource(MemTag::Editor)};
			HashMap<StringHash, const void*> sceneIcons{GetMemoryResource(MemTag::Editor)};
			HashMap<StringHash, RadiusEntry> radii{GetMemoryResource(MemTag::Editor)};
			Vector<RadiusHandle> scratch{GetMemoryResource(MemTag::Editor)};
			HashMap<StringHash, SceneHandleEntry> sceneHandles{GetMemoryResource(MemTag::Editor)};
			Vector<SceneHandle> handleScratch{GetMemoryResource(MemTag::Editor)};
		};

		GizmoState* _state = nullptr;

		Vec3 PerpendicularTo(const Vec3& axis) {
			const Vec3 reference = Math::Abs(axis.y) > 0.9f ? Vec3::Forward() : Vec3::Up();
			return Vec3::Cross(axis, reference).Normalized();
		}

		void AppendBoxEdges(Vector<DebugLine>& out, const Vec3 (&corners)[8], Color32 color) {
			constexpr u32 EDGES[12][2] = {
				{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
			for (const auto& edge : EDGES) {
				out.push_back({corners[edge[0]], corners[edge[1]], color});
			}
		}

		void AppendArc(Vector<DebugLine>& out, const Vec3& center, const Vec3& fromAxis, const Vec3& towardAxis,
					   f32 radius, Color32 color) {
			constexpr u32 SEGMENTS = 16;
			Vec3 previous = center + fromAxis * radius;
			for (u32 i = 1; i <= SEGMENTS; ++i) {
				const f32 angle = static_cast<f32>(i) / SEGMENTS * Math::PI;
				const Vec3 point = center + (fromAxis * std::cos(angle) + towardAxis * std::sin(angle)) * radius;
				out.push_back({previous, point, color});
				previous = point;
			}
		}

		// reverse Z (near = 1, far = 0)
		void AppendFrustum(Vector<DebugLine>& out, const Mat4& inverseViewProjection, Color32 color) {
			const Vec3 clip[8] = {
				{-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
			Vec3 corners[8];
			for (u32 i = 0; i < 8; ++i) {
				corners[i] = inverseViewProjection.ProjectPoint(clip[i]);
			}
			AppendBoxEdges(out, corners, color);
		}

		void DrawCameraGizmo(GizmoDrawContext& context, Handle<Component> component) {
			Camera* camera = static_cast<Camera*>(component.Get());
			if (!camera) {
				return;
			}
			SceneObject* owner = camera->GetOwner().Get();
			if (!owner) {
				return;
			}
			if (!context.selected) {
				return;
			}
			const Vec3 position = owner->GetWorldPosition();
			const Quat rotation = owner->GetWorldRotation();
			const Color32 color = CameraWire();
			const Mat4 view = Mat4::TRS(position, rotation, Vec3::One()).Inverse();
			const Mat4 projection = camera->BuildProjection(context.viewAspect <= 0.0f ? 1.0f : context.viewAspect);
			AppendFrustum(*context.lines, (projection * view).Inverse(), color);
		}

		void DrawLightGizmo(GizmoDrawContext& context, Handle<Component> component) {
			Light* light = static_cast<Light*>(component.Get());
			if (!light) {
				return;
			}
			SceneObject* owner = light->GetOwner().Get();
			if (!owner) {
				return;
			}
			if (!context.selected) {
				return;
			}
			const Vec3 position = owner->GetWorldPosition();
			const Quat rotation = owner->GetWorldRotation();
			const Vec3 forward = rotation * Vec3::Forward();
			const Color32 color = LightWire();
			switch (light->GetLightType()) {
			case LightType::Directional: {
				constexpr u32 RAYS = 8;
				constexpr f32 RADIUS = 0.35f;
				constexpr f32 LENGTH = 1.6f;
				const Vec3 basisX = PerpendicularTo(forward);
				const Vec3 basisY = Vec3::Cross(forward, basisX);
				EditorGizmos::AppendCircle(*context.lines, position, forward, RADIUS, color);
				for (u32 i = 0; i < RAYS; ++i) {
					const f32 angle = static_cast<f32>(i) / RAYS * Math::TAU;
					const Vec3 start = position + (basisX * std::cos(angle) + basisY * std::sin(angle)) * RADIUS;
					context.lines->push_back({start, start + forward * LENGTH, color});
				}
				break;
			}
			case LightType::Point:
				EditorGizmos::AppendCircle(*context.lines, position, Vec3::Right(), light->GetRange(), color);
				EditorGizmos::AppendCircle(*context.lines, position, Vec3::Up(), light->GetRange(), color);
				EditorGizmos::AppendCircle(*context.lines, position, Vec3::Forward(), light->GetRange(), color);
				break;
			case LightType::Spot: {
				const f32 halfAngle = Math::Clamp(light->GetSpotAngleDeg(), 1.0f, 179.0f) * 0.5f * Math::DEG_TO_RAD;
				const f32 capRadius = std::tan(halfAngle) * light->GetRange();
				const Vec3 capCenter = position + forward * light->GetRange();
				EditorGizmos::AppendCircle(*context.lines, capCenter, forward, capRadius, color);
				const Vec3 basisX = PerpendicularTo(forward);
				const Vec3 basisY = Vec3::Cross(forward, basisX);
				for (u32 i = 0; i < 4; ++i) {
					const f32 angle = static_cast<f32>(i) / 4.0f * Math::TAU;
					const Vec3 rim = capCenter + (basisX * std::cos(angle) + basisY * std::sin(angle)) * capRadius;
					context.lines->push_back({position, rim, color});
				}
				break;
			}
			}
		}

		void DrawEnvironmentGizmo(GizmoDrawContext& context, Handle<Component> component) {
			Environment* environment = static_cast<Environment*>(component.Get());
			if (!context.selected || !environment || environment->GetIsGlobal()) {
				return;
			}
			SceneObject* owner = environment->GetOwner().Get();
			if (!owner) {
				return;
			}
			const Vec3 scale = owner->GetLossyScale();
			const Vec3 size = environment->GetSize();
			const Vec3 halfExtents(size.x * scale.x * 0.5f, size.y * scale.y * 0.5f, size.z * scale.z * 0.5f);
			EditorGizmos::AppendOrientedBox(*context.lines, owner->GetWorldPosition(), owner->GetWorldRotation(), halfExtents,
											EditorTheme::GetColor32(ColorToken::GizmoVolumeWire));
		}

		struct WalkContext {
			GizmoDrawContext* draw = nullptr;
		};

		void GizmoVisitor(SceneObject& object, void* user) {
			WalkContext& walk = *static_cast<WalkContext*>(user);
			if (!object.IsActiveInHierarchy()) {
				return;
			}
			const bool ownerSelected = EditorSelection::IsSelected(Handle<Object>(&object));
			const u32 count = object.GetComponentCount();
			for (u32 i = 0; i < count; ++i) {
				Handle<Component> component = object.GetComponentAt(i);
				Component* raw = component.Get();
				if (!raw) {
					continue;
				}
				const TypeInfo* type = raw->GetType();
				if (!type) {
					continue;
				}
				const StringHash key(type->name);
				const ComponentGizmoFn* gizmo = EditorGizmos::FindComponentGizmo(key);
				const bool wantsIcon = walk.draw->icons && _state->sceneIcons.contains(key);
				if (!gizmo && !wantsIcon) {
					continue;
				}
				const bool selected = ownerSelected || EditorSelection::IsSelected(Handle<Object>(raw));
				if (wantsIcon) {
					walk.draw->icons->push_back(
						SceneIcon{Handle<SceneObject>(&object), type, object.GetWorldPosition()});
				}
				if (gizmo) {
					walk.draw->selected = selected;
					(*gizmo)(*walk.draw, component);
				}
			}
		}

	} // anonymous namespace

	void EditorGizmos::AppendCircle(Vector<DebugLine>& out, const Vec3& center, const Vec3& normal, f32 radius, Color32 color) {
		constexpr u32 SEGMENTS = 32;
		const Vec3 basisX = PerpendicularTo(normal);
		const Vec3 basisY = Vec3::Cross(normal, basisX);
		Vec3 previous = center + basisX * radius;
		for (u32 i = 1; i <= SEGMENTS; ++i) {
			const f32 angle = static_cast<f32>(i) / SEGMENTS * Math::TAU;
			const Vec3 point = center + (basisX * std::cos(angle) + basisY * std::sin(angle)) * radius;
			out.push_back({previous, point, color});
			previous = point;
		}
	}

	void EditorGizmos::AppendOrientedBox(Vector<DebugLine>& out, const Vec3& center, const Quat& rotation,
										 const Vec3& halfExtents, Color32 color) {
		const Vec3 x = rotation * Vec3(halfExtents.x, 0.0f, 0.0f);
		const Vec3 y = rotation * Vec3(0.0f, halfExtents.y, 0.0f);
		const Vec3 z = rotation * Vec3(0.0f, 0.0f, halfExtents.z);
		const Vec3 corners[8] = {
			center - x - y - z, center + x - y - z, center + x + y - z, center - x + y - z,
			center - x - y + z, center + x - y + z, center + x + y + z, center - x + y + z};
		AppendBoxEdges(out, corners, color);
	}

	void EditorGizmos::AppendCapsule(Vector<DebugLine>& out, const Vec3& pointA, const Vec3& pointB, f32 radius, Color32 color) {
		const Vec3 axis = pointB - pointA;
		const f32 length = axis.Length();
		if (length <= Math::EPSILON) {
			AppendCircle(out, pointA, Vec3::Right(), radius, color);
			AppendCircle(out, pointA, Vec3::Up(), radius, color);
			AppendCircle(out, pointA, Vec3::Forward(), radius, color);
			return;
		}

		const Vec3 up = axis / length;
		const Vec3 basisX = PerpendicularTo(up);
		const Vec3 basisY = Vec3::Cross(up, basisX);

		AppendCircle(out, pointA, up, radius, color);
		AppendCircle(out, pointB, up, radius, color);

		const Vec3 rim[4] = {basisX, basisY, -basisX, -basisY};
		for (const Vec3& direction : rim) {
			out.push_back({pointA + direction * radius, pointB + direction * radius, color});
		}
		AppendArc(out, pointA, basisX, -up, radius, color);
		AppendArc(out, pointA, basisY, -up, radius, color);
		AppendArc(out, pointB, basisX, up, radius, color);
		AppendArc(out, pointB, basisY, up, radius, color);
	}

	void EditorGizmos::Initialize() {
		if (_state) {
			return;
		}
		_state = New<GizmoState>(MemTag::Editor);
		RegisterComponentGizmo(StringHash("Camera"), &DrawCameraGizmo);
		RegisterComponentGizmo(StringHash("Light"), &DrawLightGizmo);
		RegisterComponentGizmo(StringHash("Environment"), &DrawEnvironmentGizmo);
		RegisterSceneIcon(StringHash("Camera"));
		RegisterSceneIcon(StringHash("Light"));
	}

	void EditorGizmos::Terminate() {
		if (_state) {
			Delete(_state);
			_state = nullptr;
		}
	}

	bool EditorGizmos::RegisterComponentGizmo(StringHash componentTypeName, ComponentGizmoFn gizmo, const void* owner) {
		if (!_state || !gizmo) {
			SLT_LOG_WARN("Editor", "a component gizmo was registered before EditorGizmos::Initialize (discarded)");
			return false;
		}
		_state->gizmos[componentTypeName] = {std::move(gizmo), owner};
		return true;
	}

	bool EditorGizmos::RegisterSceneGizmo(StringHash name, SceneGizmoFn gizmo, const void* owner) {
		if (!_state || !gizmo) {
			SLT_LOG_WARN("Editor", "a scene gizmo was registered before EditorGizmos::Initialize (discarded)");
			return false;
		}
		_state->sceneGizmos[name] = {std::move(gizmo), owner};
		return true;
	}

	bool EditorGizmos::RegisterSceneIcon(StringHash componentTypeName, const void* owner) {
		if (!_state) {
			SLT_LOG_WARN("Editor", "a scene icon was registered before EditorGizmos::Initialize (discarded)");
			return false;
		}
		_state->sceneIcons[componentTypeName] = owner;
		return true;
	}

	bool EditorGizmos::RegisterRadiusHandles(StringHash componentTypeName, RadiusHandlesFn handles, const void* owner) {
		if (!_state || !handles) {
			SLT_LOG_WARN("Editor", "a component's radius handles were registered before EditorGizmos::Initialize (discarded)");
			return false;
		}
		_state->radii[componentTypeName] = {std::move(handles), owner};
		return true;
	}

	void EditorGizmos::GatherRadiusHandles(Vector<ComponentRadius>& out) {
		out.clear();
		if (!_state || _state->radii.empty()) {
			return;
		}
		const auto offer = [&out](Component* component) {
			const TypeInfo* type = component ? component->GetType() : nullptr;
			auto it = type ? _state->radii.find(StringHash(type->name)) : _state->radii.end();
			if (it == _state->radii.end()) {
				return;
			}
			_state->scratch.clear();
			it->second.handles(Handle<Component>(component), _state->scratch);
			for (RadiusHandle& handle : _state->scratch) {
				out.push_back({Handle<Component>(component), std::move(handle)});
			}
		};
		for (const Handle<Object>& selected : EditorSelection::GetSelection()) {
			Object* object = selected.Get();
			if (!object) {
				continue;
			}
			if (TypeRegistry::IsDerivedFrom(object->GetType(), SceneObject::GetReflectType())) {
				SceneObject& owner = *static_cast<SceneObject*>(object);
				if (!owner.IsActiveInHierarchy()) {
					continue;
				}
				for (u32 index = 0; index < owner.GetComponentCount(); ++index) {
					offer(owner.GetComponentAt(index).Get());
				}
			} else if (TypeRegistry::IsDerivedFrom(object->GetType(), Component::GetReflectType())) {
				Component* component = static_cast<Component*>(object);
				const SceneObject* owner = component->GetOwner().Get();
				if (owner && owner->IsActiveInHierarchy() && !EditorSelection::IsSelected(Handle<Object>(component->GetOwner().Get()))) {
					offer(component);
				}
			}
		}
	}

	bool EditorGizmos::RegisterSceneHandles(StringHash name, SceneHandlesFn handles, SceneHandleWriteFn write, const void* owner) {
		if (!_state || !handles || !write) {
			SLT_LOG_WARN("Editor", "a tool's scene handles were registered before EditorGizmos::Initialize (discarded)");
			return false;
		}
		_state->sceneHandles[name] = {std::move(handles), std::move(write), owner};
		return true;
	}

	void EditorGizmos::GatherSceneHandles(Vector<OfferedSceneHandle>& out) {
		out.clear();
		if (!_state) {
			return;
		}
		for (const auto& [name, entry] : _state->sceneHandles) {
			_state->handleScratch.clear();
			entry.handles(_state->handleScratch);
			for (SceneHandle& handle : _state->handleScratch) {
				out.push_back({name, std::move(handle)});
			}
		}
	}

	bool EditorGizmos::WriteSceneHandle(StringHash source, const SceneHandle& dragged) {
		if (!_state) {
			return false;
		}
		const auto it = _state->sceneHandles.find(source);
		if (it == _state->sceneHandles.end()) {
			return false;
		}
		const SceneHandleWriteFn write = it->second.write;
		write(dragged);
		return true;
	}

	const ComponentGizmoFn* EditorGizmos::FindComponentGizmo(StringHash componentTypeName) {
		if (!_state) {
			return nullptr;
		}
		auto it = _state->gizmos.find(componentTypeName);
		return it != _state->gizmos.end() ? &it->second.gizmo : nullptr;
	}

	u32 EditorGizmos::UnregisterOwnedBy(const void* owner) {
		if (!_state || !owner) {
			return 0;
		}
		u32 removed = 0;
		for (auto it = _state->gizmos.begin(); it != _state->gizmos.end();) {
			if (it->second.owner == owner) {
				it = _state->gizmos.erase(it);
				++removed;
			} else {
				++it;
			}
		}
		for (auto it = _state->sceneGizmos.begin(); it != _state->sceneGizmos.end();) {
			if (it->second.owner == owner) {
				it = _state->sceneGizmos.erase(it);
				++removed;
			} else {
				++it;
			}
		}
		for (auto it = _state->sceneIcons.begin(); it != _state->sceneIcons.end();) {
			if (it->second == owner) {
				it = _state->sceneIcons.erase(it);
				++removed;
			} else {
				++it;
			}
		}
		for (auto it = _state->radii.begin(); it != _state->radii.end();) {
			if (it->second.owner == owner) {
				it = _state->radii.erase(it);
				++removed;
			} else {
				++it;
			}
		}
		for (auto it = _state->sceneHandles.begin(); it != _state->sceneHandles.end();) {
			if (it->second.owner == owner) {
				it = _state->sceneHandles.erase(it);
				++removed;
			} else {
				++it;
			}
		}
		return removed;
	}

	void EditorGizmos::AppendCascadeFitLines(Vector<DebugLine>& out, std::span<const CascadeFit> fits, i32 cascadeIndex) {
		if (cascadeIndex >= static_cast<i32>(fits.size())) {
			return;
		}
		const Color32 color = EditorTheme::GetColor32(ColorToken::GizmoShadowFit);
		const u32 first = cascadeIndex < 0 ? 0u : static_cast<u32>(cascadeIndex);
		const u32 last = cascadeIndex < 0 ? static_cast<u32>(fits.size()) : first + 1u;
		for (u32 i = first; i < last; ++i) {
			AppendFrustum(out, fits[i].viewProjection.Inverse(), color);
		}
	}

	void EditorGizmos::AppendViewFrustumLines(Vector<DebugLine>& out, const Mat4& viewProjection) {
		AppendFrustum(out, viewProjection.Inverse(), EditorTheme::GetColor32(ColorToken::GizmoShadowFitCamera));
	}

	void EditorGizmos::LayOutSceneIcons(std::span<const SceneIcon> icons, const Mat4& viewProjection,
										const Vec2& viewportSizePx, f32 sizePx, Vector<SceneIconPlacement>& out) {
		out.clear();
		if (viewportSizePx.x <= 0.0f || viewportSizePx.y <= 0.0f || sizePx <= 0.0f) {
			return;
		}
		const f32 half = sizePx * 0.5f;
		for (size_t first = 0; first < icons.size();) {
			size_t last = first + 1;
			while (last < icons.size() && icons[last].object == icons[first].object) {
				++last;
			}
			Vec2 center;
			f32 depth = 0.0f;
			if (EditorViewportOps::ProjectToViewport(viewProjection, icons[first].worldPosition,
													 viewportSizePx, center, depth)) {
				const u32 count = static_cast<u32>(last - first);
				const f32 spread = (static_cast<f32>(count) - 1.0f) * 0.5f;
				for (u32 i = 0; i < count; ++i) {
					SceneIconPlacement placement;
					placement.object = icons[first + i].object;
					placement.type = icons[first + i].type;
					placement.centerPx = {center.x + (static_cast<f32>(i) - spread) * sizePx, center.y};
					placement.sortDepth = depth;
					if (placement.centerPx.x + half >= 0.0f && placement.centerPx.x - half <= viewportSizePx.x &&
						placement.centerPx.y + half >= 0.0f && placement.centerPx.y - half <= viewportSizePx.y) {
						out.push_back(placement);
					}
				}
			}
			first = last;
		}
		std::stable_sort(out.begin(), out.end(), [](const SceneIconPlacement& a, const SceneIconPlacement& b) {
			return a.sortDepth < b.sortDepth;
		});
	}

	u32 EditorGizmos::HitSceneIcon(std::span<const SceneIconPlacement> placements, const Vec2& localPx, f32 sizePx) {
		const f32 half = sizePx * 0.5f;
		for (size_t i = placements.size(); i-- > 0;) {
			const Vec2 offset = localPx - placements[i].centerPx;
			if (Math::Abs(offset.x) <= half && Math::Abs(offset.y) <= half) {
				return static_cast<u32>(i);
			}
		}
		return SCENE_ICON_NONE;
	}

	void EditorGizmos::DrawSceneGizmos(GizmoDrawContext& context) {
		if (!_state || !context.lines || !context.overlayLines) {
			return;
		}
		for (auto& entry : _state->sceneGizmos) {
			context.selected = false;
			entry.second.gizmo(context);
		}
		const bool wantsIcons = context.icons && !_state->sceneIcons.empty();
		if (_state->gizmos.empty() && !wantsIcons) {
			return;
		}
		WalkContext walk;
		walk.draw = &context;
		Vector<Handle<Scene>> scenes{GetMemoryResource(MemTag::Editor)};
		EditorSceneOps::GetStageScenes(scenes);
		for (const Handle<Scene>& sceneHandle : scenes) {
			if (Scene* scene = sceneHandle.Get()) {
				scene->ForEachObject(&GizmoVisitor, &walk);
			}
		}
	}

} // namespace Shalltear
