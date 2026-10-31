#include "Shalltear/Editor/EditorGizmoOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"

#include "Shalltear/Core/Scene/SceneObject.h"

#include <cmath>

namespace Shalltear::EditorGizmoOps {

	namespace {

		using EditorTheme::ColorToken;

		Color32 AxisColor(u32 axis) {
			return EditorTheme::GetColor32(axis == 0   ? ColorToken::AxisX
										   : axis == 1 ? ColorToken::AxisY
													   : ColorToken::AxisZ);
		}
		Color32 Highlight() { return EditorTheme::GetColor32(ColorToken::GizmoHighlight); }
		Color32 ScreenColor() { return EditorTheme::GetColor32(ColorToken::GizmoScreen); }

		Color32 Faded(Color32 color, f32 alphaScale) {
			return Color32(color.r, color.g, color.b,
						   static_cast<u8>(Math::Clamp(static_cast<f32>(color.a) * alphaScale, 0.0f, 255.0f)));
		}

		Vec3 AxisVector(u32 axis) {
			return axis == 0 ? Vec3::Right() : (axis == 1 ? Vec3::Up() : Vec3::Forward());
		}

		GizmoHandle AxisHandle(u32 axis) {
			return static_cast<GizmoHandle>(static_cast<u8>(GizmoHandle::AxisX) + static_cast<u8>(axis));
		}
		GizmoHandle PlaneHandle(u32 plane) {
			return static_cast<GizmoHandle>(static_cast<u8>(GizmoHandle::PlaneYZ) + static_cast<u8>(plane));
		}

		Vec3 PerpendicularTo(const Vec3& axis) {
			Vec3 reference = Math::Abs(axis.y) > 0.9f ? Vec3::Forward() : Vec3::Up();
			return Vec3::Cross(axis, reference).Normalized();
		}

		Vec3 ViewAxis(const GizmoDesc& desc) {
			return EditorViewportOps::ViewAxisAt(desc.viewpoint, desc.origin);
		}

		Vec3 WorldAxis(const GizmoDesc& desc, u32 axis) {
			return (desc.orientation * AxisVector(axis)).Normalized();
		}

		void PlaneBasis(const GizmoDesc& desc, u32 plane, Vec3& outA, Vec3& outB, Vec3& outNormal) {
			outNormal = WorldAxis(desc, plane);
			outA = WorldAxis(desc, (plane + 1) % 3);
			outB = WorldAxis(desc, (plane + 2) % 3);
			const Vec3 toGizmo = -ViewAxis(desc);
			if (Vec3::Dot(outA, toGizmo) > 0.0f) {
				outA = -outA;
			}
			if (Vec3::Dot(outB, toGizmo) > 0.0f) {
				outB = -outB;
			}
		}

		bool OnFacingHalf(const GizmoDesc& desc, const Vec3& offsetFromOrigin) {
			const f32 length = offsetFromOrigin.Length();
			if (length < 1.0e-8f) {
				return true;
			}
			return Vec3::Dot(offsetFromOrigin, ViewAxis(desc)) / length > -RING_BACK_HALF_COS;
		}

		f32 WrapToPi(f32 radians) {
			return radians - Math::TAU * std::round(radians / Math::TAU);
		}

		bool AxisSolvePlane(const GizmoDesc& desc, const Vec3& axis, Vec3& outNormal) {
			Vec3 viewDirection = desc.viewpoint.rotation * Vec3::Forward();
			Vec3 normal = viewDirection - axis * Vec3::Dot(viewDirection, axis);
			if (normal.LengthSquared() < 1.0e-6f) {
				return false;
			}
			outNormal = normal.Normalized();
			return true;
		}

		bool RaycastPlaneAt(const Vec3& origin, const Vec3& normal, const Ray& ray, Vec3& outPoint) {
			Plane plane = Plane::FromPointNormal(origin, normal);
			f32 enter = 0.0f;
			if (!plane.Raycast(ray, enter)) {
				return false;
			}
			outPoint = ray.GetPoint(enter);
			return true;
		}

		f32 RayToSegmentDistance(const Ray& ray, const Vec3& origin, const Vec3& direction, f32 length, f32& outRayT) {
			const Vec3 u = ray.direction;
			const Vec3 v = direction;
			const Vec3 w0 = ray.origin - origin;
			const f32 a = Vec3::Dot(u, u);
			const f32 b = Vec3::Dot(u, v);
			const f32 c = Vec3::Dot(v, v);
			const f32 d = Vec3::Dot(u, w0);
			const f32 e = Vec3::Dot(v, w0);
			const f32 denominator = a * c - b * b;

			f32 t = 0.0f;
			if (Math::Abs(denominator) > 1.0e-6f) {
				t = (a * e - b * d) / denominator;
			} else {
				t = -e / (c > 1.0e-6f ? c : 1.0f);
			}
			t = Math::Clamp(t, 0.0f, length);

			const Vec3 closestOnSegment = origin + v * t;
			f32 s = Vec3::Dot(closestOnSegment - ray.origin, u) / (a > 1.0e-6f ? a : 1.0f);
			s = Math::Max(s, 0.0f);

			outRayT = s;
			return Vec3::Distance(ray.GetPoint(s), closestOnSegment);
		}

		constexpr u32 RING_SEGMENTS = 64;

		template <typename Visit>
		void ForEachRingSegment(const GizmoDesc& desc, const Vec3& axis, f32 radius, bool facingHalfOnly,
								Visit&& visit) {
			const Vec3 basisX = PerpendicularTo(axis);
			const Vec3 basisY = Vec3::Cross(axis, basisX);
			Vec3 previous = desc.origin + basisX * radius;
			for (u32 i = 1; i <= RING_SEGMENTS; ++i) {
				const f32 angle = static_cast<f32>(i) / RING_SEGMENTS * Math::TAU;
				const Vec3 point = desc.origin + (basisX * std::cos(angle) + basisY * std::sin(angle)) * radius;
				if (!facingHalfOnly || OnFacingHalf(desc, (previous + point) * 0.5f - desc.origin)) {
					visit(previous, point);
				}
				previous = point;
			}
		}

		bool HitRing(const GizmoDesc& desc, const Vec3& axis, f32 radius, bool facingHalfOnly,
					 const Ray& ray, f32& outRayT) {
			if (Math::Abs(Vec3::Dot(ray.direction, axis)) < RING_GRAZING_MINIMUM) {
				return false;
			}

			bool found = false;
			f32 nearest = 0.0f;
			ForEachRingSegment(desc, axis, radius, facingHalfOnly, [&](const Vec3& from, const Vec3& to) {
				const Vec3 edge = to - from;
				const f32 length = edge.Length();
				if (length < 1.0e-8f) {
					return;
				}
				f32 rayT = 0.0f;
				if (RayToSegmentDistance(ray, from, edge / length, length, rayT) > desc.pickTolerance) {
					return;
				}
				if (!found || rayT < nearest) {
					found = true;
					nearest = rayT;
				}
			});
			outRayT = nearest;
			return found;
		}

		bool HitPlaneQuad(const GizmoDesc& desc, u32 plane, const Ray& ray, f32& outRayT) {
			Vec3 a, b, normal;
			PlaneBasis(desc, plane, a, b, normal);
			Vec3 point;
			if (!RaycastPlaneAt(desc.origin, normal, ray, point)) {
				return false;
			}
			const Vec3 offset = point - desc.origin;
			const f32 u = Vec3::Dot(offset, a);
			const f32 v = Vec3::Dot(offset, b);
			const f32 low = desc.scale * PLANE_HANDLE_OFFSET;
			const f32 high = low + desc.scale * PLANE_HANDLE_SIZE;
			if (u < low - desc.pickTolerance || u > high + desc.pickTolerance ||
				v < low - desc.pickTolerance || v > high + desc.pickTolerance) {
				return false;
			}
			outRayT = Vec3::Distance(point, ray.origin);
			return true;
		}

		bool HitScreenSquare(const GizmoDesc& desc, const Ray& ray, f32& outRayT) {
			const Vec3 normal = ViewAxis(desc);
			Vec3 point;
			if (!RaycastPlaneAt(desc.origin, normal, ray, point)) {
				return false;
			}
			if (Vec3::Distance(point, desc.origin) > desc.scale * SCREEN_HANDLE_HALF + desc.pickTolerance) {
				return false;
			}
			outRayT = Vec3::Distance(point, ray.origin);
			return true;
		}

		u8 PriorityOf(GizmoHandle handle) {
			switch (handle) {
			case GizmoHandle::Screen:
				return 2;
			case GizmoHandle::PlaneYZ:
			case GizmoHandle::PlaneZX:
			case GizmoHandle::PlaneXY:
				return 1;
			default:
				return 0;
			}
		}

		void AppendArc(Vector<DebugLine>& out, const GizmoDesc& desc, const Vec3& axis, f32 radius,
					   bool facingHalfOnly, Color32 color) {
			ForEachRingSegment(desc, axis, radius, facingHalfOnly,
							   [&](const Vec3& from, const Vec3& to) { out.push_back({from, to, color}); });
		}

		void AppendArrowHead(Vector<DebugLine>& out, const Vec3& tip, const Vec3& direction, f32 scale, Color32 color) {
			constexpr u32 SEGMENTS = 8;
			const f32 length = scale * 0.22f;
			const f32 radius = scale * 0.07f;
			const Vec3 base = tip - direction * length;
			const Vec3 side = PerpendicularTo(direction);
			const Vec3 up = Vec3::Cross(direction, side).Normalized();
			Vec3 previous = base + side * radius;
			for (u32 i = 1; i <= SEGMENTS; ++i) {
				const f32 angle = static_cast<f32>(i) / SEGMENTS * Math::TAU;
				const Vec3 point = base + (side * std::cos(angle) + up * std::sin(angle)) * radius;
				out.push_back({previous, point, color});
				out.push_back({point, tip, color});
				previous = point;
			}
		}

		void AppendCubeCap(Vector<DebugLine>& out, const Vec3& center, const Vec3& direction, f32 scale, Color32 color) {
			const f32 half = scale * 0.07f;
			const Vec3 x = direction * half;
			const Vec3 y = PerpendicularTo(direction) * half;
			const Vec3 z = Vec3::Cross(direction, PerpendicularTo(direction)).Normalized() * half;
			const Vec3 corners[8] = {
				center - x - y - z, center + x - y - z, center + x + y - z, center - x + y - z,
				center - x - y + z, center + x - y + z, center + x + y + z, center - x + y + z};
			constexpr u32 EDGES[12][2] = {
				{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
			for (const auto& edge : EDGES) {
				out.push_back({corners[edge[0]], corners[edge[1]], color});
			}
		}

		void AppendPlaneQuad(Vector<DebugLine>& out, const GizmoDesc& desc, u32 plane, Color32 color) {
			Vec3 a, b, normal;
			PlaneBasis(desc, plane, a, b, normal);
			const f32 low = desc.scale * PLANE_HANDLE_OFFSET;
			const f32 size = desc.scale * PLANE_HANDLE_SIZE;
			const Vec3 p0 = desc.origin + a * low + b * low;
			const Vec3 p1 = p0 + a * size;
			const Vec3 p2 = p1 + b * size;
			const Vec3 p3 = p0 + b * size;
			out.push_back({p0, p1, color});
			out.push_back({p1, p2, color});
			out.push_back({p2, p3, color});
			out.push_back({p3, p0, color});
			const Color32 fill = Faded(color, 0.45f);
			out.push_back({p0, p2, fill});
			out.push_back({p1, p3, fill});
		}

		void AppendScreenSquare(Vector<DebugLine>& out, const GizmoDesc& desc, Color32 color) {
			const Vec3 normal = ViewAxis(desc);
			const Vec3 right = PerpendicularTo(normal) * (desc.scale * SCREEN_HANDLE_HALF);
			const Vec3 up = Vec3::Cross(normal, PerpendicularTo(normal)).Normalized() * (desc.scale * SCREEN_HANDLE_HALF);
			const Vec3 p0 = desc.origin - right - up;
			const Vec3 p1 = desc.origin + right - up;
			const Vec3 p2 = desc.origin + right + up;
			const Vec3 p3 = desc.origin - right + up;
			out.push_back({p0, p1, color});
			out.push_back({p1, p2, color});
			out.push_back({p2, p3, color});
			out.push_back({p3, p0, color});
		}

		Color32 ColorFor(GizmoHandle handle, GizmoHandle hovered, GizmoHandle dragged) {
			const bool lit = dragged != GizmoHandle::None ? dragged == handle : hovered == handle;
			if (lit) {
				return Highlight();
			}
			switch (handle) {
			case GizmoHandle::AxisX:
			case GizmoHandle::PlaneYZ:
				return AxisColor(0);
			case GizmoHandle::AxisY:
			case GizmoHandle::PlaneZX:
				return AxisColor(1);
			case GizmoHandle::AxisZ:
			case GizmoHandle::PlaneXY:
				return AxisColor(2);
			default:
				return ScreenColor();
			}
		}

		constexpr f32 PIVOT_OFFSET_EPSILON_SQUARED = 1.0e-12f;

		void MoveAboutPivot(const DragSnapshot& start, const Vec3& pivot, const Vec3& newOffset, DragOutcome& outcome) {
			if ((start.worldPosition - pivot).LengthSquared() <= PIVOT_OFFSET_EPSILON_SQUARED) {
				return;
			}
			outcome.localPosition = start.worldToParent.TransformPoint(pivot + newOffset);
			outcome.positionChanged = true;
		}

		Vec3 RatioOrKeep(const Vec3& result, const Vec3& base, const Vec3& fallback) {
			return Vec3{base.x != 0.0f ? result.x / base.x : fallback.x,
						base.y != 0.0f ? result.y / base.y : fallback.y,
						base.z != 0.0f ? result.z / base.z : fallback.z};
		}

	} // anonymous namespace

	bool HandleIsInMode(GizmoHandle handle, GizmoMode mode) {
		switch (handle) {
		case GizmoHandle::None:
			return false;
		case GizmoHandle::AxisX:
		case GizmoHandle::AxisY:
		case GizmoHandle::AxisZ:
			return true;
		case GizmoHandle::PlaneYZ:
		case GizmoHandle::PlaneZX:
		case GizmoHandle::PlaneXY:
		case GizmoHandle::Screen:
			return mode != GizmoMode::Rotate;
		case GizmoHandle::ScreenRotate:
			return mode == GizmoMode::Rotate;
		}
		return false;
	}

	Quat OrientationForSpace(GizmoSpace space, GizmoMode mode, const Quat& targetWorldRotation) {
		if (space == GizmoSpace::Local || mode == GizmoMode::Scale) {
			return targetWorldRotation;
		}
		return Quat::Identity();
	}

	f32 SnapToIncrement(f32 value, f32 increment) {
		if (increment <= 0.0f) {
			return value;
		}
		return std::round(value / increment) * increment;
	}

	Vec3 SnapChangedComponents(const Vec3& value, const Vec3& reference, f32 increment) {
		if (increment <= 0.0f) {
			return value;
		}
		Vec3 result = value;
		for (size_t axis = 0; axis < 3; ++axis) {
			if (!Math::Approximately(value[axis], reference[axis])) {
				result[axis] = SnapToIncrement(value[axis], increment);
			} else {
				result[axis] = reference[axis];
			}
		}
		return result;
	}

	f32 HandleScaleAt(const EditorViewportOps::Viewpoint& viewpoint, const Vec3& origin) {
		return Math::Max(EditorViewportOps::ViewHeightAt(viewpoint, origin) * GIZMO_VIEW_FRACTION, 0.001f);
	}

	f32 WorldRadiusForPixels(const EditorViewportOps::Viewpoint& viewpoint, const Vec3& point,
							 f32 viewportHeightPx, f32 pixels) {
		if (viewportHeightPx <= 0.0f) {
			return 0.0f;
		}
		return (EditorViewportOps::ViewHeightAt(viewpoint, point) / viewportHeightPx) * pixels;
	}

	void AppendGizmo(Vector<DebugLine>& out, const GizmoDesc& desc, GizmoHandle hovered, GizmoHandle dragged) {
		if (desc.mode == GizmoMode::Rotate) {
			for (u32 axis = 0; axis < 3; ++axis) {
				AppendArc(out, desc, WorldAxis(desc, axis), desc.scale, true,
						  ColorFor(AxisHandle(axis), hovered, dragged));
			}
			AppendArc(out, desc, ViewAxis(desc), desc.scale * SCREEN_RING_SCALE, false,
					  ColorFor(GizmoHandle::ScreenRotate, hovered, dragged));
			return;
		}

		for (u32 axis = 0; axis < 3; ++axis) {
			const GizmoHandle handle = AxisHandle(axis);
			const Color32 color = ColorFor(handle, hovered, dragged);
			const Vec3 direction = WorldAxis(desc, axis);
			const Vec3 tip = desc.origin + direction * desc.scale;
			out.push_back({desc.origin, tip, color});
			if (desc.mode == GizmoMode::Translate) {
				AppendArrowHead(out, tip, direction, desc.scale, color);
			} else {
				AppendCubeCap(out, tip, direction, desc.scale, color);
			}
		}
		for (u32 plane = 0; plane < 3; ++plane) {
			AppendPlaneQuad(out, desc, plane, ColorFor(PlaneHandle(plane), hovered, dragged));
		}
		AppendScreenSquare(out, desc, ColorFor(GizmoHandle::Screen, hovered, dragged));
	}

	void AppendVertexMarker(Vector<DebugLine>& out, const Vec3& world, const Quat& cameraRotation, f32 halfSize,
							Color32 color) {
		const Vec3 right = (cameraRotation * Vec3::Right()) * halfSize;
		const Vec3 up = (cameraRotation * Vec3::Up()) * halfSize;
		const Vec3 p0 = world - right - up;
		const Vec3 p1 = world + right - up;
		const Vec3 p2 = world + right + up;
		const Vec3 p3 = world - right + up;
		out.push_back({p0, p1, color});
		out.push_back({p1, p2, color});
		out.push_back({p2, p3, color});
		out.push_back({p3, p0, color});
	}

	GizmoHit HitTest(const GizmoDesc& desc, const Ray& ray) {
		GizmoHit best;
		u8 bestPriority = 0;

		const auto consider = [&](GizmoHandle handle, f32 rayT) {
			const u8 priority = PriorityOf(handle);
			if (best.handle == GizmoHandle::None || priority > bestPriority ||
				(priority == bestPriority && rayT < best.distance)) {
				bestPriority = priority;
				best = {handle, rayT};
			}
		};

		f32 rayT = 0.0f;
		if (desc.mode == GizmoMode::Rotate) {
			for (u32 axis = 0; axis < 3; ++axis) {
				if (HitRing(desc, WorldAxis(desc, axis), desc.scale, true, ray, rayT)) {
					consider(AxisHandle(axis), rayT);
				}
			}
			if (HitRing(desc, ViewAxis(desc), desc.scale * SCREEN_RING_SCALE, false, ray, rayT)) {
				consider(GizmoHandle::ScreenRotate, rayT);
			}
			return best;
		}

		for (u32 axis = 0; axis < 3; ++axis) {
			if (RayToSegmentDistance(ray, desc.origin, WorldAxis(desc, axis), desc.scale, rayT) <= desc.pickTolerance) {
				consider(AxisHandle(axis), rayT);
			}
		}
		for (u32 plane = 0; plane < 3; ++plane) {
			if (HitPlaneQuad(desc, plane, ray, rayT)) {
				consider(PlaneHandle(plane), rayT);
			}
		}
		if (HitScreenSquare(desc, ray, rayT)) {
			consider(GizmoHandle::Screen, rayT);
		}
		return best;
	}

	bool BeginDrag(const GizmoDesc& desc, GizmoHandle handle, const Ray& ray, DragState& out) {
		if (!HandleIsInMode(handle, desc.mode)) {
			return false;
		}

		DragState state;
		state.handle = handle;
		state.origin = desc.origin;
		state.referenceScale = desc.scale;

		switch (handle) {
		case GizmoHandle::AxisX:
		case GizmoHandle::AxisY:
		case GizmoHandle::AxisZ: {
			const u32 axis = static_cast<u32>(handle) - static_cast<u32>(GizmoHandle::AxisX);
			state.axisWorld = WorldAxis(desc, axis);
			state.dragDirection = state.axisWorld;
			state.scaleMask = AxisVector(axis);
			if (desc.mode == GizmoMode::Rotate) {
				state.planeNormal = state.axisWorld;
			} else if (!AxisSolvePlane(desc, state.axisWorld, state.planeNormal)) {
				return false;
			}
			break;
		}
		case GizmoHandle::PlaneYZ:
		case GizmoHandle::PlaneZX:
		case GizmoHandle::PlaneXY: {
			const u32 plane = static_cast<u32>(handle) - static_cast<u32>(GizmoHandle::PlaneYZ);
			Vec3 a, b, normal;
			PlaneBasis(desc, plane, a, b, normal);
			state.planeNormal = normal;
			state.axisWorld = normal;
			state.dragDirection = (a + b).Normalized();
			state.scaleMask = Vec3::One() - AxisVector(plane);
			break;
		}
		case GizmoHandle::Screen:
			state.planeNormal = ViewAxis(desc);
			state.axisWorld = state.planeNormal;
			state.scaleMask = Vec3::One();
			break;
		case GizmoHandle::ScreenRotate:
			state.planeNormal = ViewAxis(desc);
			state.axisWorld = state.planeNormal;
			state.scaleMask = Vec3::One();
			break;
		default:
			return false;
		}

		Vec3 grab;
		if (!RaycastPlaneAt(state.origin, state.planeNormal, ray, grab)) {
			return false;
		}
		state.grabPoint = grab;

		if (desc.mode == GizmoMode::Rotate) {
			const Vec3 offset = grab - state.origin;
			if (offset.LengthSquared() < 1.0e-8f) {
				return false;
			}
			const Vec3 basisX = PerpendicularTo(state.axisWorld);
			const Vec3 basisY = Vec3::Cross(state.axisWorld, basisX);
			state.previousAngle = std::atan2(Vec3::Dot(offset, basisY), Vec3::Dot(offset, basisX));
			state.accumulatedAngle = 0.0f;
		} else if (desc.mode == GizmoMode::Scale && state.dragDirection.LengthSquared() < 1.0e-8f) {
			const Vec3 offset = grab - state.origin;
			state.dragDirection = offset.LengthSquared() > 1.0e-8f
									  ? offset.Normalized()
									  : (desc.viewpoint.rotation * Vec3::Right()).Normalized();
		}

		out = state;
		return true;
	}

	bool UpdateDrag(const GizmoDesc& desc, DragState& state, const Ray& ray, DragValue& outValue) {
		if (state.handle == GizmoHandle::None) {
			return false;
		}
		Vec3 point;
		if (!RaycastPlaneAt(state.origin, state.planeNormal, ray, point)) {
			return false;
		}

		outValue = DragValue{};

		if (desc.mode == GizmoMode::Rotate) {
			const Vec3 offset = point - state.origin;
			if (offset.LengthSquared() < 1.0e-8f) {
				return false;
			}
			const Vec3 basisX = PerpendicularTo(state.axisWorld);
			const Vec3 basisY = Vec3::Cross(state.axisWorld, basisX);
			const f32 raw = std::atan2(Vec3::Dot(offset, basisY), Vec3::Dot(offset, basisX));
			state.accumulatedAngle += WrapToPi(raw - state.previousAngle);
			state.previousAngle = raw;
			outValue.angleRadians = state.accumulatedAngle;
			return true;
		}

		const Vec3 delta = point - state.grabPoint;
		if (desc.mode == GizmoMode::Translate) {
			outValue.translation = state.handle == GizmoHandle::AxisX || state.handle == GizmoHandle::AxisY ||
										   state.handle == GizmoHandle::AxisZ
									   ? state.axisWorld * Vec3::Dot(delta, state.axisWorld)
									   : delta;
			return true;
		}

		const f32 factor = Math::Max(1.0f + Vec3::Dot(delta, state.dragDirection) / state.referenceScale, 0.01f);
		outValue.scaleMultiplier = Vec3::One() + state.scaleMask * (factor - 1.0f);
		return true;
	}

	Vec3 PivotFor(std::span<const Handle<SceneObject>> targets) {
		Vec3 sum = Vec3::Zero();
		u32 counted = 0;
		for (const Handle<SceneObject>& target : targets) {
			if (SceneObject* object = target.Get()) {
				sum = sum + object->GetWorldPosition();
				++counted;
			}
		}
		return counted == 0 ? Vec3::Zero() : sum * (1.0f / static_cast<f32>(counted));
	}

	void SnapshotTargets(std::span<const Handle<SceneObject>> targets, Vector<DragSnapshot>& out) {
		out.clear();
		for (const Handle<SceneObject>& target : targets) {
			SceneObject* object = target.Get();
			if (!object) {
				continue;
			}
			DragSnapshot snapshot;
			snapshot.object = target;
			snapshot.localPosition = object->GetLocalPosition();
			snapshot.localRotation = object->GetLocalRotation();
			snapshot.localScale = object->GetLocalScale();
			snapshot.worldPosition = object->GetWorldPosition();
			snapshot.worldRotation = object->GetWorldRotation();
			if (SceneObject* parent = object->GetParent().Get()) {
				snapshot.parentToWorld = parent->GetWorldMatrix();
				snapshot.worldToParent = snapshot.parentToWorld.Inverse();
				snapshot.parentWorldRotation = parent->GetWorldRotation();
			}
			out.push_back(snapshot);
		}
	}

	void SolveDragTargets(std::span<const DragSnapshot> starts, GizmoMode mode, const DragState& drag,
						  const DragValue& value, const Vec3& pivot, const Quat& gizmoOrientation,
						  const SnapSettings& snap, Vector<DragOutcome>& out) {
		out.clear();
		out.resize(starts.size());
		if (starts.empty()) {
			return;
		}
		const DragSnapshot& reference = starts[0];

		switch (mode) {
		case GizmoMode::Translate: {
			Vec3 worldDelta = value.translation;
			Vec3 referenceResult = reference.localPosition + reference.worldToParent.TransformVector(worldDelta);
			if (snap.translate > 0.0f) {
				referenceResult = SnapChangedComponents(referenceResult, reference.localPosition, snap.translate);
				worldDelta = reference.parentToWorld.TransformVector(referenceResult - reference.localPosition);
			}
			for (size_t i = 0; i < starts.size(); ++i) {
				out[i].localPosition = i == 0 ? referenceResult
											  : starts[i].localPosition +
													starts[i].worldToParent.TransformVector(worldDelta);
				out[i].positionChanged = true;
			}
			break;
		}
		case GizmoMode::Rotate: {
			f32 angleDeg = value.angleRadians * Math::RAD_TO_DEG;
			if (snap.rotateDeg > 0.0f) {
				angleDeg = SnapToIncrement(angleDeg, snap.rotateDeg);
			}
			const Quat spin = Quat::AngleAxis(angleDeg, drag.axisWorld);
			for (size_t i = 0; i < starts.size(); ++i) {
				const DragSnapshot& start = starts[i];
				const Quat parentWorld = start.parentWorldRotation;
				out[i].localRotation = (parentWorld.Inverse() * spin * parentWorld * start.localRotation).Normalized();
				out[i].rotationChanged = true;
				MoveAboutPivot(start, pivot, spin * (start.worldPosition - pivot), out[i]);
			}
			break;
		}
		case GizmoMode::Scale: {
			Vec3 multiplier = value.scaleMultiplier;
			Vec3 referenceResult = reference.localScale * multiplier;
			if (snap.scale > 0.0f) {
				referenceResult = SnapChangedComponents(referenceResult, reference.localScale, snap.scale);
				multiplier = RatioOrKeep(referenceResult, reference.localScale, multiplier);
			}
			const Quat toBasis = gizmoOrientation.Inverse();
			for (size_t i = 0; i < starts.size(); ++i) {
				const DragSnapshot& start = starts[i];
				out[i].localScale = i == 0 ? referenceResult : start.localScale * multiplier;
				out[i].scaleChanged = true;
				const Vec3 offset = start.worldPosition - pivot;
				MoveAboutPivot(start, pivot, gizmoOrientation * (toBasis * offset * multiplier), out[i]);
			}
			break;
		}
		}
	}

	u32 RadiusDots(const EditorViewportOps::Viewpoint& viewpoint, const Vec3& center, const Quat& rotation, f32 radius, RadiusDot (&out)[6]) {
		if (!(radius > 0.0f)) {
			return 0;
		}
		const Vec3 toViewer = EditorViewportOps::ViewAxisAt(viewpoint, center);
		f32 frontDegrees = 90.0f;
		if (!viewpoint.orthographic) {
			const f32 distance = Vec3::Distance(viewpoint.position, center);
			frontDegrees = distance > radius ? std::acos(radius / distance) * Math::RAD_TO_DEG : -1000.0f;
		}
		const f32 skip = std::cos(RADIUS_AXIS_MIN_DEGREES * Math::DEG_TO_RAD);
		u32 count = 0;
		for (u32 index = 0; index < 6; ++index) {
			const Vec3 local = index < 3 ? AxisVector(index) : AxisVector(index - 3) * -1.0f;
			const Vec3 axis = (rotation * local).Normalized();
			const f32 facing = Vec3::Dot(axis, toViewer);
			if (Math::Abs(facing) > skip) {
				continue;
			}
			const f32 degrees = std::acos(Math::Clamp(facing, -1.0f, 1.0f)) * Math::RAD_TO_DEG;
			out[count++] = RadiusDot{center + axis * radius, axis, degrees <= frontDegrees + RADIUS_AXIS_MIN_DEGREES};
		}
		return count;
	}

	i32 HitRadiusDots(std::span<const RadiusDot> dots, const EditorViewportOps::Viewpoint& viewpoint, f32 viewportHeightPx, const Ray& ray,
					  f32& outRayT) {
		i32 best = -1;
		f32 bestT = 0.0f;
		for (size_t index = 0; index < dots.size(); ++index) {
			const Vec3& position = dots[index].position;
			const f32 along = Math::Max(0.0f, Vec3::Dot(position - ray.origin, ray.direction));
			const f32 gap = Vec3::Distance(ray.GetPoint(along), position);
			if (gap <= WorldRadiusForPixels(viewpoint, position, viewportHeightPx, HANDLE_PICK_PX) && (best < 0 || along < bestT)) {
				best = static_cast<i32>(index);
				bestT = along;
			}
		}
		outRayT = bestT;
		return best;
	}

	void AppendRadiusDots(Vector<DebugLine>& out, std::span<const RadiusDot> dots, const EditorViewportOps::Viewpoint& viewpoint, Color32 color,
						  i32 highlighted) {
		for (size_t index = 0; index < dots.size(); ++index) {
			const RadiusDot& dot = dots[index];
			const Color32 ink = static_cast<i32>(index) == highlighted ? Highlight() : dot.front ? color
																								 : Faded(color, 0.5f);
			const f32 half = HandleScaleAt(viewpoint, dot.position) * RADIUS_DOT_HALF;
			AppendVertexMarker(out, dot.position, viewpoint.rotation, half, ink);
			const Vec3 right = (viewpoint.rotation * Vec3::Right()) * half;
			const Vec3 up = (viewpoint.rotation * Vec3::Up()) * half;
			out.push_back({dot.position - right - up, dot.position + right + up, ink});
			out.push_back({dot.position + right - up, dot.position - right + up, ink});
		}
	}

	bool BeginRadiusDrag(const EditorViewportOps::Viewpoint& viewpoint, const Vec3& center, const RadiusDot& dot, f32 radius, const Ray& ray,
						 RadiusDrag& out) {
		const Vec3 viewDirection = viewpoint.rotation * Vec3::Forward();
		const Vec3 normal = viewDirection - dot.axis * Vec3::Dot(viewDirection, dot.axis);
		if (normal.LengthSquared() < 1.0e-6f) {
			return false;
		}
		RadiusDrag drag;
		drag.center = center;
		drag.axis = dot.axis;
		drag.planeNormal = normal.Normalized();
		drag.startRadius = radius;
		Vec3 point;
		if (!RaycastPlaneAt(center, drag.planeNormal, ray, point)) {
			return false;
		}
		drag.grabAlong = Vec3::Dot(point - center, drag.axis);
		out = drag;
		return true;
	}

	bool UpdateRadiusDrag(const RadiusDrag& drag, const Ray& ray, f32& outRadius) {
		Vec3 point;
		if (!RaycastPlaneAt(drag.center, drag.planeNormal, ray, point)) {
			return false;
		}
		outRadius = Math::Abs(drag.startRadius + Vec3::Dot(point - drag.center, drag.axis) - drag.grabAlong);
		return true;
	}

	bool SceneHandleMode(SceneHandleKind kind, GizmoMode tool, GizmoMode& out) {
		switch (kind) {
		case SceneHandleKind::Transform:
			out = tool;
			return true;
		case SceneHandleKind::Position:
			out = GizmoMode::Translate;
			return true;
		case SceneHandleKind::Rotation:
			out = GizmoMode::Rotate;
			return tool == GizmoMode::Rotate;
		case SceneHandleKind::Slider:
			return false;
		}
		return false;
	}

	Vec3 SceneHandleOrigin(const SceneHandle& handle) { return handle.parentToWorld.TransformPoint(handle.position); }

	Quat SceneHandleRotation(const SceneHandle& handle) { return (handle.parentRotation * handle.rotation).Normalized(); }

	SceneHandle SolveSceneHandleDrag(const SceneHandle& start, GizmoMode mode, const DragState& drag, const DragValue& value, const Quat& gizmoOrientation,
									 const SnapSettings& snap) {
		DragSnapshot snapshot;
		snapshot.localPosition = start.position;
		snapshot.localRotation = start.rotation;
		snapshot.localScale = start.scale;
		snapshot.worldPosition = SceneHandleOrigin(start);
		snapshot.worldRotation = SceneHandleRotation(start);
		snapshot.parentToWorld = start.parentToWorld;
		Mat4 inverse;
		snapshot.worldToParent = start.parentToWorld.TryInvert(inverse) ? inverse : Mat4::Identity();
		snapshot.parentWorldRotation = start.parentRotation;
		Vector<DragOutcome> solved{GetMemoryResource(MemTag::Editor)};
		SolveDragTargets({&snapshot, 1}, mode, drag, value, drag.origin, gizmoOrientation, snap, solved);
		SceneHandle out = start;
		if (!solved.empty()) {
			out.position = solved[0].positionChanged ? solved[0].localPosition : start.position;
			out.rotation = solved[0].rotationChanged ? solved[0].localRotation : start.rotation;
			out.scale = solved[0].scaleChanged ? solved[0].localScale : start.scale;
		}
		return out;
	}

	RadiusDot SliderDot(const SceneHandle& handle) {
		const Vec3 axis = handle.parentToWorld.TransformVector(handle.direction).Normalized();
		return RadiusDot{handle.parentToWorld.TransformPoint(handle.position + handle.direction * handle.distance), axis, true};
	}

	bool BeginSliderDrag(const EditorViewportOps::Viewpoint& viewpoint, const SceneHandle& handle, const Ray& ray, RadiusDrag& out) {
		const RadiusDot dot = SliderDot(handle);
		const f32 unit = handle.parentToWorld.TransformVector(handle.direction).Length();
		if (!(unit > 0.0f)) {
			return false;
		}
		return BeginRadiusDrag(viewpoint, handle.parentToWorld.TransformPoint(handle.position), dot, handle.distance * unit, ray, out);
	}

	bool UpdateSliderDrag(const RadiusDrag& drag, const SceneHandle& start, const Ray& ray, f32& outDistance) {
		Vec3 point;
		const f32 unit = start.parentToWorld.TransformVector(start.direction).Length();
		if (!(unit > 0.0f) || !RaycastPlaneAt(drag.center, drag.planeNormal, ray, point)) {
			return false;
		}
		outDistance = (drag.startRadius + Vec3::Dot(point - drag.center, drag.axis) - drag.grabAlong) / unit;
		return true;
	}

} // namespace Shalltear::EditorGizmoOps
