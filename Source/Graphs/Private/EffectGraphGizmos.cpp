#include "EffectGraphGizmos.h"

#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphEffectBlocks.h"
#include "GraphNodeLibrary.h"
#include "GraphWindowParts.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Shalltear::EffectGraphGizmoOps {

	namespace {

		namespace B = GraphEffectBlocks;

		enum Role : u32 {
			RoleTransform = 1,
			RolePosition = 2,
			RoleRotation = 3,
			RoleSlider = 16
		};

		constexpr u32 Id(u32 port, u32 role) { return (port << 8) | role; }
		constexpr u32 PortOf(u32 id) { return id >> 8; }
		constexpr u32 RoleOf(u32 id) { return id & 0xffu; }

		constexpr f32 PLANE_HALF = 5.0f;
		constexpr f32 ARROW_LENGTH = 1.0f;
		constexpr u32 RING_SEGMENTS = 32;

		struct Ports {
			const GraphNode* node = nullptr;
			const GraphNodeDefinition* definition = nullptr;
			Vector<u32> active{GetMemoryResource(MemTag::Editor)};

			bool Open(const GraphDocument& document, u32 block, GraphDefinitions& definitions) {
				node = nullptr;
				for (const GraphContext& context : document.contexts) {
					for (const GraphNode& held : context.blocks) {
						node = held.id == block ? &held : node;
					}
				}
				for (const GraphNode& held : document.nodes) {
					node = held.id == block ? &held : node;
				}
				definition = node ? definitions.Find(document, *node) : nullptr;
				if (!definition) {
					return false;
				}
				GraphWindowParts::ActiveInputs(*definition, *node, active);
				return true;
			}

			i32 Index(std::string_view name) const {
				for (u32 index : active) {
					if (definition->inputs[index].name == name) {
						return static_cast<i32>(index);
					}
				}
				return -1;
			}

			const GraphPortDefinition& Port(u32 index) const { return definition->inputs[index]; }

			bool Read(u32 index, f32 (&out)[4]) const {
				const GraphPortDefinition& port = Port(index);
				std::copy(std::begin(port.defaultValue), std::end(port.defaultValue), out);
				const GraphInput* input = GraphWindowParts::FindInput(*node, port.name);
				if (!input) {
					return true;
				}
				if (input->fromNode != 0) {
					return false;
				}
				if (!input->value.empty()) {
					f32 read[4] = {};
					const u32 count = GraphWindowParts::ReadNumbers(input->value, read);
					std::copy(read, read + std::min<u32>(count, 4), out);
				}
				return true;
			}

			bool World(u32 index) const {
				const GraphInput* input = GraphWindowParts::FindInput(*node, Port(index).name);
				return input && input->space == GRAPH_SPACE_WORLD;
			}
		};

		Vec3 V3(const f32 (&v)[4]) { return Vec3(v[0], v[1], v[2]); }

		// degrees, Z -> X -> Y (like Quat::FromEuler)
		Quat Turn(const Vec3& angles) { return Quat::FromEuler(angles); }

		Quat FromTo(const Vec3& from, const Vec3& to) {
			const Vec3 a = from.Normalized();
			const Vec3 b = to.Normalized();
			const f32 cosine = Math::Clamp(Vec3::Dot(a, b), -1.0f, 1.0f);
			Vec3 axis = Vec3::Cross(a, b);
			if (axis.LengthSquared() < 1.0e-12f) {
				if (cosine > 0.0f) {
					return Quat::Identity();
				}
				axis = Math::Abs(a.x) < 0.9f ? Vec3::Cross(a, Vec3::Right()) : Vec3::Cross(a, Vec3::Up());
			}
			return Quat::AngleAxis(std::acos(cosine) * Math::RAD_TO_DEG, axis);
		}

		void AppendArc(Vector<DebugLine>& out, const Mat4& toWorld, const Vec3& center, const Vec3& from, const Vec3& normal, f32 sweepDegrees,
					   Color32 color) {
			const f32 sweep = Math::Clamp(sweepDegrees, -360.0f, 360.0f);
			const u32 segments = std::max<u32>(1, static_cast<u32>(std::ceil(RING_SEGMENTS * Math::Abs(sweep) / 360.0f)));
			Vec3 previous = toWorld.TransformPoint(center + from);
			for (u32 i = 1; i <= segments; ++i) {
				const Quat turn = Quat::AngleAxis(sweep * static_cast<f32>(i) / static_cast<f32>(segments), normal);
				const Vec3 point = toWorld.TransformPoint(center + turn * from);
				out.push_back({previous, point, color});
				previous = point;
			}
		}

		void AppendSegment(Vector<DebugLine>& out, const Mat4& toWorld, const Vec3& a, const Vec3& b, Color32 color) {
			out.push_back({toWorld.TransformPoint(a), toWorld.TransformPoint(b), color});
		}

		struct Offer {
			Mat4 effectToWorld;
			Quat effectRotation;
			Color32 wire;
			Color32 dot;
			str label;
			BlockGizmo* out = nullptr;

			Mat4 Space(bool world) const { return world ? Mat4::Identity() : effectToWorld; }
			Quat SpaceTurn(bool world) const { return world ? Quat::Identity() : effectRotation; }

			SceneHandle Frame(u32 id, SceneHandleKind kind, bool world, const Vec3& position, const Quat& rotation, const Vec3& scale) const {
				SceneHandle handle;
				handle.id = id;
				handle.kind = kind;
				handle.parentToWorld = Space(world);
				handle.parentRotation = SpaceTurn(world);
				handle.position = position;
				handle.rotation = rotation;
				handle.scale = scale;
				handle.label = label;
				return handle;
			}

			void Slider(u32 id, const Mat4& frame, const Vec3& from, const Vec3& direction, f32 distance) const {
				SceneHandle handle;
				handle.id = id;
				handle.kind = SceneHandleKind::Slider;
				handle.parentToWorld = frame;
				handle.position = from;
				handle.direction = direction;
				handle.distance = distance;
				handle.label = label;
				handle.color = dot;
				out->handles.push_back(std::move(handle));
			}
		};

		struct TransformPorts {
			u32 position = 0;
			u32 angles = 0;
			u32 scale = 0;
			bool box = false;
		};

		bool TransformAt(const Ports& ports, u32 index, TransformPorts& out) {
			const GraphPortDefinition& port = ports.Port(index);
			if (port.space != GraphPortSpace::Matrix) {
				return false;
			}
			const std::string_view name = port.name;
			std::string_view prefix;
			if (name.ends_with("Position")) {
				prefix = name.substr(0, name.size() - 8);
			} else if (name.ends_with("Center")) {
				prefix = name.substr(0, name.size() - 6);
			} else {
				return false;
			}
			const i32 angles = ports.Index(str(prefix) + "Angles");
			const i32 scale = ports.Index(str(prefix) + "Scale");
			const i32 size = ports.Index(str(prefix) + "Size");
			if (angles < 0 || (scale < 0 && size < 0)) {
				return false;
			}
			out = {index, static_cast<u32>(angles), static_cast<u32>(scale >= 0 ? scale : size), scale < 0};
			return true;
		}

		enum class Shape : u8 { None,
								Sphere,
								Circle,
								Cone,
								Torus,
								Box,
								Line,
								Plane,
								AttractorSphere };

		u32 Chosen(const GraphNode& node, std::string_view key, std::span<const std::string_view> options, u32 fallback) {
			const GraphSetting* setting = GraphWindowParts::FindSetting(node.settings, key);
			if (!setting) {
				return fallback;
			}
			const str value = GraphWindowParts::ReadString(setting->value);
			const auto found = std::find(options.begin(), options.end(), std::string_view(value));
			return found == options.end() ? fallback : static_cast<u32>(found - options.begin());
		}

		Shape ShapeOf(const GraphNode& node) {
			if (node.def == GRAPH_EFFECT_COLLISION_SHAPE) {
				switch (Chosen(node, "Shape", B::COLLISION_SHAPES, B::CollidePlane)) {
				case B::CollideSphere:
					return Shape::Sphere;
				case B::CollideBox:
					return Shape::Box;
				case B::CollideCone:
					return Shape::Cone;
				case B::CollidePlane:
					return Shape::Plane;
				default:
					return Shape::None;
				}
			}
			if (node.def == GRAPH_EFFECT_POSITION_SHAPE) {
				switch (Chosen(node, "Shape", B::POSITION_SHAPES, B::Sphere)) {
				case B::Sphere:
					return Shape::Sphere;
				case B::OrientedBox:
					return Shape::Box;
				case B::Cone:
					return Shape::Cone;
				case B::Torus:
					return Shape::Torus;
				case B::Circle:
					return Shape::Circle;
				case B::Line:
					return Shape::Line;
				default:
					return Shape::None;
				}
			}
			if (node.def == GRAPH_EFFECT_CONFORM_TO_SPHERE) {
				return Shape::AttractorSphere;
			}
			return Shape::None;
		}

		bool Scalar(const Ports& ports, std::string_view name, f32 fallback, f32& out, bool& linked) {
			out = fallback;
			const i32 index = ports.Index(name);
			if (index < 0) {
				return false;
			}
			f32 value[4] = {};
			if (!ports.Read(static_cast<u32>(index), value)) {
				linked = true;
				return false;
			}
			out = value[0];
			return true;
		}

		bool OfferTransform(const Ports& ports, const TransformPorts& t, Shape shape, const Offer& offer) {
			f32 p[4], a[4], s[4];
			if (!ports.Read(t.position, p) || !ports.Read(t.angles, a) || !ports.Read(t.scale, s)) {
				return false;
			}
			bool linked = false;
			f32 radius = 1.0f, arc = 360.0f, base = 1.0f, top = 0.1f, height = 1.0f, major = 1.0f, minor = 0.1f;
			Scalar(ports, "Radius", 1.0f, radius, linked);
			Scalar(ports, "Arc", 360.0f, arc, linked);
			Scalar(ports, "Base Radius", 1.0f, base, linked);
			Scalar(ports, "Top Radius", 0.1f, top, linked);
			Scalar(ports, "Height", 1.0f, height, linked);
			Scalar(ports, "Major Radius", 1.0f, major, linked);
			Scalar(ports, "Minor Radius", 0.1f, minor, linked);
			if (linked) {
				return false;
			}
			const bool world = ports.World(t.position);
			const Mat4 space = offer.Space(world);
			BlockGizmo& out = *offer.out;
			if (t.box) {
				const Vec3 size = V3(s);
				const Mat4 frame = space * Mat4::TRS(V3(p), Turn(V3(a)), Vec3(1.0f, 1.0f, 1.0f));
				const Vec3 h = size * 0.5f;
				const Vec3 corners[8] = {{h.x, h.y, h.z}, {h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, -h.y, h.z}, {h.x, h.y, -h.z}, {h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {-h.x, -h.y, -h.z}};
				constexpr u32 EDGES[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 6}, {3, 7}, {4, 6}, {5, 7}};
				for (const auto& edge : EDGES) {
					AppendSegment(out.lines, frame, corners[edge[0]], corners[edge[1]], offer.wire);
				}
				out.handles.push_back(offer.Frame(Id(t.position, RolePosition), SceneHandleKind::Position, world, V3(p), Turn(V3(a)), Vec3(1.0f, 1.0f, 1.0f)));
				out.handles.push_back(offer.Frame(Id(t.position, RoleRotation), SceneHandleKind::Rotation, world, V3(p), Turn(V3(a)), Vec3(1.0f, 1.0f, 1.0f)));
				for (u32 axis = 0; axis < 3; ++axis) {
					for (u32 side = 0; side < 2; ++side) {
						Vec3 direction;
						direction[axis] = side == 0 ? 1.0f : -1.0f;
						offer.Slider(Id(t.position, RoleSlider + axis * 2 + side), frame, Vec3(), direction, size[axis] * 0.5f);
					}
				}
				return true;
			}
			const Mat4 frame = space * Mat4::TRS(V3(p), Turn(V3(a)), V3(s));
			out.handles.push_back(offer.Frame(Id(t.position, RoleTransform), SceneHandleKind::Transform, world, V3(p), Turn(V3(a)), V3(s)));
			const u32 slider = Id(t.position, RoleSlider);
			switch (shape) {
			case Shape::Sphere:
				if (arc >= 360.0f) {
					AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, radius, 0.0f), Vec3::Forward(), 360.0f, offer.wire);
					AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, 0.0f, radius), Vec3::Up(), 360.0f, offer.wire);
					AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, radius, 0.0f), Vec3::Right(), 360.0f, offer.wire);
				} else {
					AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, radius, 0.0f), Vec3::Forward() * -1.0f, arc, offer.wire);
					for (f32 at : {0.0f, arc}) {
						const Vec3 rim = Quat::AngleAxis(at, Vec3::Forward() * -1.0f) * Vec3(0.0f, radius, 0.0f);
						const Vec3 normal = Vec3::Cross(rim, Vec3::Forward());
						AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, 0.0f, radius), normal, 180.0f, offer.wire);
					}
				}
				for (u32 axis = 0; axis < 3; ++axis) {
					Vec3 direction;
					direction[axis] = 1.0f;
					offer.Slider(slider + axis, frame, Vec3(), direction, radius);
				}
				return true;
			case Shape::Circle:
				AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, radius, 0.0f), Vec3::Forward() * -1.0f, Math::Min(arc, 360.0f), offer.wire);
				offer.Slider(slider, frame, Vec3(), Vec3::Right(), radius);
				offer.Slider(slider + 1, frame, Vec3(), Vec3::Up(), radius);
				return true;
			case Shape::Cone: {
				const f32 sweep = Math::Min(arc, 360.0f);
				AppendArc(out.lines, frame, Vec3(0.0f, height, 0.0f), Vec3(0.0f, 0.0f, top), Vec3::Up(), sweep, offer.wire);
				AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, 0.0f, base), Vec3::Up(), sweep, offer.wire);
				for (u32 quarter = 0; quarter < 4 && static_cast<f32>(quarter) * 90.0f <= sweep; ++quarter) {
					const Quat turn = Quat::AngleAxis(static_cast<f32>(quarter) * 90.0f, Vec3::Up());
					AppendSegment(out.lines, frame, turn * Vec3(0.0f, 0.0f, base), Vec3(0.0f, height, 0.0f) + turn * Vec3(0.0f, 0.0f, top), offer.wire);
				}
				for (u32 quarter = 0; quarter < 4; ++quarter) {
					const Vec3 direction = Quat::AngleAxis(static_cast<f32>(quarter) * 90.0f, Vec3::Up()) * Vec3::Forward();
					offer.Slider(slider + quarter, frame, Vec3(), direction, base);
					offer.Slider(slider + 4 + quarter, frame, Vec3(0.0f, height, 0.0f), direction, top);
				}
				offer.Slider(slider + 8, frame, Vec3(), Vec3::Up(), height);
				return true;
			}
			case Shape::Torus: {
				const f32 sweep = Math::Min(arc, 360.0f);
				const Vec3 normal = Vec3::Forward() * -1.0f;
				AppendArc(out.lines, frame, Vec3(0.0f, 0.0f, minor), Vec3(0.0f, major, 0.0f), normal, sweep, offer.wire);
				AppendArc(out.lines, frame, Vec3(0.0f, 0.0f, -minor), Vec3(0.0f, major, 0.0f), normal, sweep, offer.wire);
				AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, major + minor, 0.0f), normal, sweep, offer.wire);
				AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, major - minor, 0.0f), normal, sweep, offer.wire);
				offer.Slider(slider, frame, Vec3(), Vec3::Up(), major);
				offer.Slider(slider + 1, frame, Vec3(), Vec3::Right(), major);
				offer.Slider(slider + 2, frame, Vec3(0.0f, major, 0.0f), Vec3::Up(), minor);
				return true;
			}
			default:
				return true;
			}
		}

		bool OfferPlane(const Ports& ports, const Offer& offer) {
			const i32 position = ports.Index("Position");
			const i32 normal = ports.Index("Normal");
			f32 p[4], n[4];
			if (position < 0 || normal < 0 || !ports.Read(static_cast<u32>(position), p) || !ports.Read(static_cast<u32>(normal), n)) {
				return false;
			}
			const bool world = ports.World(static_cast<u32>(position));
			const Mat4 space = offer.Space(world);
			Vec3 facing = V3(n);
			if (ports.World(static_cast<u32>(normal)) != world) {
				const Mat4 from = offer.Space(!world);
				Mat4 inverse;
				facing = space.TryInvert(inverse) ? inverse.TransformVector(from.TransformVector(facing)) : facing;
			}
			if (facing.LengthSquared() < 1.0e-12f) {
				facing = Vec3::Up();
			}
			const Quat turn = FromTo(Vec3::Forward(), facing);
			const Mat4 frame = space * Mat4::TRS(V3(p), turn, Vec3(1.0f, 1.0f, 1.0f));
			BlockGizmo& out = *offer.out;
			const Vec3 corners[4] = {{PLANE_HALF, PLANE_HALF, 0.0f}, {PLANE_HALF, -PLANE_HALF, 0.0f}, {-PLANE_HALF, -PLANE_HALF, 0.0f}, {-PLANE_HALF, PLANE_HALF, 0.0f}};
			for (u32 i = 0; i < 4; ++i) {
				AppendSegment(out.lines, frame, corners[i], corners[(i + 1) % 4], offer.wire);
			}
			const Vec3 tip(0.0f, 0.0f, ARROW_LENGTH);
			AppendSegment(out.lines, frame, Vec3(), tip, offer.wire);
			for (const Vec3& barb : {Vec3(0.1f, 0.0f, -0.2f), Vec3(-0.1f, 0.0f, -0.2f), Vec3(0.0f, 0.1f, -0.2f), Vec3(0.0f, -0.1f, -0.2f)}) {
				AppendSegment(out.lines, frame, tip, tip + barb, offer.wire);
			}
			out.handles.push_back(offer.Frame(Id(static_cast<u32>(position), RolePosition), SceneHandleKind::Position, world, V3(p), Quat::Identity(),
											  Vec3(1.0f, 1.0f, 1.0f)));
			out.handles.push_back(
				offer.Frame(Id(static_cast<u32>(position), RoleRotation), SceneHandleKind::Rotation, world, V3(p), turn, Vec3(1.0f, 1.0f, 1.0f)));
			return true;
		}

		bool OfferAttractor(const Ports& ports, const Offer& offer) {
			const i32 center = ports.Index("Center");
			f32 c[4];
			f32 radius = 1.0f;
			bool linked = false;
			if (center < 0 || !ports.Read(static_cast<u32>(center), c) || !Scalar(ports, "Radius", 1.0f, radius, linked)) {
				return false;
			}
			const bool world = ports.World(static_cast<u32>(center));
			const Mat4 frame = offer.Space(world) * Mat4::Translation(V3(c));
			BlockGizmo& out = *offer.out;
			AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, radius, 0.0f), Vec3::Forward(), 360.0f, offer.wire);
			AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, 0.0f, radius), Vec3::Up(), 360.0f, offer.wire);
			AppendArc(out.lines, frame, Vec3(), Vec3(0.0f, radius, 0.0f), Vec3::Right(), 360.0f, offer.wire);
			out.handles.push_back(offer.Frame(Id(static_cast<u32>(center), RolePosition), SceneHandleKind::Position, world, V3(c), Quat::Identity(),
											  Vec3(1.0f, 1.0f, 1.0f)));
			for (u32 axis = 0; axis < 3; ++axis) {
				Vec3 direction;
				direction[axis] = 1.0f;
				offer.Slider(Id(static_cast<u32>(center), RoleSlider + axis), frame, Vec3(), direction, radius);
			}
			return true;
		}

		bool TransformStartingAt(const Ports& ports, u32 port, TransformPorts& out) {
			return port < ports.definition->inputs.size() && std::find(ports.active.begin(), ports.active.end(), port) != ports.active.end() &&
				   TransformAt(ports, port, out);
		}

		bool Store(GraphDocument& document, u32 block, const Ports& ports, u32 index, const f32* values, u32 count) {
			return NodeShaderEditOps::SetInputValue(document, block, ports.Port(index).name, GraphWindowParts::Numbers(values, count));
		}

		bool StoreScalar(GraphDocument& document, u32 block, const Ports& ports, std::string_view name, f32 value) {
			const i32 index = ports.Index(name);
			return index >= 0 && Store(document, block, ports, static_cast<u32>(index), &value, 1);
		}

		bool StoreVec3(GraphDocument& document, u32 block, const Ports& ports, u32 index, const Vec3& value) {
			const f32 values[3] = {value.x, value.y, value.z};
			return Store(document, block, ports, index, values, 3);
		}

		bool Moved(const Vec3& a, const Vec3& b) { return (a - b).LengthSquared() > 1.0e-12f; }
		bool Turned(const Quat& a, const Quat& b) { return Math::Abs(Quat::Dot(a.Normalized(), b.Normalized())) < 1.0f - 1.0e-7f; }

	} // anonymous namespace

	bool Build(const GraphDocument& document, u32 block, GraphDefinitions& definitions, const Mat4& effectToWorld, const Quat& effectRotation,
			   Color32 wire, Color32 dot, std::string_view label, BlockGizmo& out) {
		out.lines.clear();
		out.handles.clear();
		Ports ports;
		if (!ports.Open(document, block, definitions)) {
			return false;
		}
		Offer offer{effectToWorld, effectRotation, wire, dot, str(label), &out};
		const Shape shape = ShapeOf(*ports.node);
		Vector<u32> taken{GetMemoryResource(MemTag::Editor)};
		if (shape == Shape::Plane) {
			if (OfferPlane(ports, offer)) {
				taken.push_back(static_cast<u32>(ports.Index("Position")));
			}
		} else if (shape == Shape::AttractorSphere) {
			if (OfferAttractor(ports, offer)) {
				taken.push_back(static_cast<u32>(ports.Index("Center")));
			}
		}
		for (const u32 index : ports.active) {
			TransformPorts transform;
			if (TransformAt(ports, index, transform)) {
				const bool first = std::none_of(out.handles.begin(), out.handles.end(), [](const SceneHandle& handle) {
					return RoleOf(handle.id) == RoleTransform || RoleOf(handle.id) == RoleRotation;
				});
				OfferTransform(ports, transform, first ? shape : Shape::None, offer);
				continue;
			}
			f32 value[4];
			if (ports.Port(index).space != GraphPortSpace::Position || std::find(taken.begin(), taken.end(), index) != taken.end() ||
				!ports.Read(index, value)) {
				continue;
			}
			out.handles.push_back(offer.Frame(Id(index, RolePosition), SceneHandleKind::Position, ports.World(index), V3(value), Quat::Identity(),
											  Vec3(1.0f, 1.0f, 1.0f)));
		}
		if (shape == Shape::Line) {
			const i32 start = ports.Index("Start");
			const i32 end = ports.Index("End");
			f32 a[4], b[4];
			if (start >= 0 && end >= 0 && ports.Read(static_cast<u32>(start), a) && ports.Read(static_cast<u32>(end), b)) {
				out.lines.push_back({offer.Space(ports.World(static_cast<u32>(start))).TransformPoint(V3(a)),
									 offer.Space(ports.World(static_cast<u32>(end))).TransformPoint(V3(b)), wire});
			}
		}
		return !out.handles.empty();
	}

	bool Write(GraphDocument& document, u32 block, GraphDefinitions& definitions, const Mat4& effectToWorld, const SceneHandle& dragged) {
		Ports ports;
		if (!ports.Open(document, block, definitions)) {
			return false;
		}
		const u32 port = PortOf(dragged.id);
		const u32 role = RoleOf(dragged.id);
		if (port >= ports.definition->inputs.size()) {
			return false;
		}
		const Shape shape = ShapeOf(*ports.node);
		bool changed = false;
		TransformPorts t;
		const bool transform = TransformStartingAt(ports, port, t);
		f32 p[4], a[4], s[4];
		if (transform && (!ports.Read(t.position, p) || !ports.Read(t.angles, a) || !ports.Read(t.scale, s))) {
			return false;
		}
		if (shape == Shape::Plane && role == RoleRotation && ports.Port(port).name == "Position") {
			const i32 normal = ports.Index("Normal");
			if (normal < 0) {
				return false;
			}
			Vec3 facing = dragged.rotation * Vec3::Forward();
			const bool world = ports.World(port);
			if (ports.World(static_cast<u32>(normal)) != world) {
				const Mat4 into = world ? effectToWorld : Mat4::Identity();
				const Mat4 from = world ? Mat4::Identity() : effectToWorld;
				Mat4 inverse;
				facing = into.TryInvert(inverse) ? inverse.TransformVector(from.TransformVector(facing)) : facing;
			}
			f32 old[4];
			if (!ports.Read(static_cast<u32>(normal), old) || !Turned(FromTo(Vec3::Forward(), V3(old)), FromTo(Vec3::Forward(), facing))) {
				return false;
			}
			return StoreVec3(document, block, ports, static_cast<u32>(normal), facing.Normalized());
		}
		switch (role) {
		case RoleTransform:
			if (!transform) {
				return false;
			}
			if (Moved(dragged.position, V3(p))) {
				changed = StoreVec3(document, block, ports, t.position, dragged.position) || changed;
			}
			if (Turned(dragged.rotation, Turn(V3(a)))) {
				changed = StoreVec3(document, block, ports, t.angles, dragged.rotation.ToEuler()) || changed;
			}
			if (Moved(dragged.scale, V3(s))) {
				changed = StoreVec3(document, block, ports, t.scale, dragged.scale) || changed;
			}
			return changed;
		case RolePosition: {
			f32 value[4];
			if (!ports.Read(port, value) || !Moved(dragged.position, V3(value))) {
				return false;
			}
			return StoreVec3(document, block, ports, port, dragged.position);
		}
		case RoleRotation:
			return transform && Turned(dragged.rotation, Turn(V3(a))) && StoreVec3(document, block, ports, t.angles, dragged.rotation.ToEuler());
		default:
			break;
		}
		if (role < RoleSlider) {
			return false;
		}
		const u32 slider = role - RoleSlider;
		const f32 reach = Math::Abs(dragged.distance);
		if (shape == Shape::AttractorSphere || (transform && !t.box && (shape == Shape::Sphere || shape == Shape::Circle))) {
			return StoreScalar(document, block, ports, "Radius", reach);
		}
		if (transform && t.box) {
			const u32 axis = slider / 2;
			if (axis > 2) {
				return false;
			}
			const f32 sign = slider % 2 == 0 ? 1.0f : -1.0f;
			Vec3 size = V3(s);
			const f32 face = sign * dragged.distance;
			const f32 other = -sign * size[axis] * 0.5f;
			const f32 grown = Math::Abs(face - other);
			if (!(Math::Abs(grown - size[axis]) > 1.0e-6f)) {
				return false;
			}
			Vec3 middle;
			middle[axis] = (face + other) * 0.5f;
			size[axis] = grown;
			changed = StoreVec3(document, block, ports, t.scale, size);
			return StoreVec3(document, block, ports, t.position, V3(p) + Turn(V3(a)) * middle) || changed;
		}
		if (transform && shape == Shape::Cone) {
			if (slider < 4) {
				return StoreScalar(document, block, ports, "Base Radius", reach);
			}
			if (slider < 8) {
				return StoreScalar(document, block, ports, "Top Radius", reach);
			}
			return slider == 8 && StoreScalar(document, block, ports, "Height", dragged.distance);
		}
		if (transform && shape == Shape::Torus) {
			return slider < 2 ? StoreScalar(document, block, ports, "Major Radius", reach)
							  : slider == 2 && StoreScalar(document, block, ports, "Minor Radius", reach);
		}
		return false;
	}

} // namespace Shalltear::EffectGraphGizmoOps
