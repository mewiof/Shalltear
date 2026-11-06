#include "Ribbon.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"

#include <cmath>

namespace Shalltear::VisualEffectsImpl {

	namespace {

		constexpr f32 MERGE_DISTANCE_SQUARED = 1e-10f;
		constexpr f32 STRAIGHT_COSINE = 0.9999f;

		Vec3 FacingAt(const RibbonFacing& facing, const Vec3& position) {
			if (!facing.fromEye) {
				return facing.axis;
			}
			const Vec3 toEye = (facing.eye - position).Normalized();
			return toEye == Vec3::Zero() ? facing.axis : toEye;
		}

		Vec3 AnyPerpendicular(const Vec3& normal) {
			const Vec3 seed = Math::Abs(normal.y) < 0.99f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
			return Vec3::Cross(normal, seed).Normalized();
		}

		Vec3 SideOf(const Vec3& direction, const Vec3& normal, const Vec3& fallback) {
			const Vec3 side = Vec3::Cross(normal, direction);
			const f32 length = side.Length();
			return length > 1e-6f ? side / length : fallback;
		}

		Vec3 RotateAbout(const Vec3& direction, const Vec3& normal, f32 radians) {
			return direction * std::cos(radians) + Vec3::Cross(normal, direction) * std::sin(radians);
		}

		struct Joint {
			MeshVertex inA;
			MeshVertex inB;
			MeshVertex outA;
			MeshVertex outB;
		};

		struct Scratch {
			Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Graphics)};
			Vector<const RibbonPoint*> kept{GetMemoryResource(MemTag::Graphics)};
			Vector<Vec3> segments{GetMemoryResource(MemTag::Graphics)};
			Vector<Joint> joints{GetMemoryResource(MemTag::Graphics)};
		};

		Scratch* _scratch = nullptr;

		Scratch& Scratched() {
			if (!_scratch) {
				_scratch = New<Scratch>(MemTag::Graphics);
			}
			return *_scratch;
		}

		class Emitter {
		public:
			Emitter(Vector<MeshVertex>& out, const Vec2& scale) : _out(out), _scale(scale) {}

			MeshVertex Make(const Vec3& position, const Vec3& normal, const Vec3& tangent, f32 u, f32 v, Color32 color) const {
				MeshVertex vertex;
				vertex.position = position;
				vertex.normal = normal;
				vertex.tangent = tangent;
				vertex.tangentSign = 1.0f;
				vertex.u = u * _scale.x;
				vertex.v = v * _scale.y;
				vertex.color = color;
				return vertex;
			}

			void Triangle(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c) {
				const Vec3 face = Vec3::Cross(b.position - a.position, c.position - a.position);
				const bool front = Vec3::Dot(face, a.normal + b.normal + c.normal) >= 0.0f;
				_out.push_back(a);
				_out.push_back(front ? b : c);
				_out.push_back(front ? c : b);
			}

			void Fan(const MeshVertex& center, const Vec3& origin, const Vec3& from, f32 sweep, u32 steps, f32 half, const Vec3& normal,
					 const Vec3& side, const MeshVertex& style) {
				MeshVertex previous = style;
				for (u32 step = 0; step <= steps; ++step) {
					const Vec3 direction = RotateAbout(from, normal, sweep * static_cast<f32>(step) / static_cast<f32>(steps));
					MeshVertex next = style;
					next.position = origin + direction * half;
					next.v = (0.5f - 0.5f * Vec3::Dot(direction, side)) * _scale.y;
					if (step > 0) {
						Triangle(center, previous, next);
					}
					previous = next;
				}
			}
		private:
			Vector<MeshVertex>& _out;
			Vec2 _scale;
		};

	} // anonymous namespace

	void FillRibbonPoints(std::span<const Vec3> positions, std::span<const f32> t, const RibbonStyle& style, bool loop, Vector<RibbonPoint>& out) {
		out.clear();
		const size_t count = positions.size();
		if (count == 0) {
			return;
		}
		const bool closes = loop && count >= 2;
		const size_t total = closes ? count + 1 : count;
		out.resize(total);
		f32 length = 0.0f;
		for (size_t i = 0; i < total; ++i) {
			const Vec3& position = positions[i % count];
			if (i > 0) {
				length += Vec3::Distance(positions[i - 1], position);
			}
			out[i].position = position;
			out[i].u = length;
		}
		const f32 segments = static_cast<f32>(total > 1 ? total - 1 : 1);
		for (size_t i = 0; i < total; ++i) {
			RibbonPoint& point = out[i];
			const f32 along = length > 0.0f ? point.u / length : static_cast<f32>(i) / segments;
			const f32 at = i < t.size() ? t[i] : (t.empty() ? along : 1.0f);
			const f32 width = (style.width && !style.width->keys.empty() ? style.width->Evaluate(at) : 1.0f) * style.widthMultiplier;
			point.width = Math::Max(width, 0.0f);
			point.color = style.color ? style.color->Evaluate(at) : Color::White();
			switch (style.textureMode) {
			case LineTextureMode::Stretch:
				point.u = along;
				break;
			case LineTextureMode::Tile:
				break;
			case LineTextureMode::DistributePerSegment:
				point.u = static_cast<f32>(i) / segments;
				break;
			case LineTextureMode::RepeatPerSegment:
				point.u = static_cast<f32>(i);
				break;
			}
		}
	}

	RibbonFacing FacingFor(LineAlignment alignment, const Vec3& transformZ, const Mat4& view, const Mat4& projection, const Vec3& eye) {
		RibbonFacing facing;
		if (alignment == LineAlignment::TransformZ) {
			const Vec3 axis = transformZ.Normalized();
			facing.axis = axis == Vec3::Zero() ? Vec3(0.0f, 0.0f, 1.0f) : axis;
			return facing;
		}
		const Vec3 back = -Vec3(view(2, 0), view(2, 1), view(2, 2)).Normalized();
		facing.axis = back == Vec3::Zero() ? Vec3(0.0f, 0.0f, -1.0f) : back;
		facing.fromEye = Math::Abs(projection(3, 2)) > 1e-6f;
		facing.eye = eye;
		return facing;
	}

	void BuildRibbon(std::span<const RibbonPoint> points, const RibbonShape& shape, const RibbonFacing& facing, Vector<MeshVertex>& out) {
		out.clear();
		Scratch& scratch = Scratched();
		Vector<const RibbonPoint*>& kept = scratch.kept;
		kept.clear();
		for (const RibbonPoint& point : points) {
			if (kept.empty() || Vec3::DistanceSquared(kept.back()->position, point.position) > MERGE_DISTANCE_SQUARED) {
				kept.push_back(&point);
			}
		}
		const size_t count = kept.size();
		if (count < 2) {
			return;
		}
		const bool closed = shape.loop && count >= 3;
		Vector<Vec3>& segments = scratch.segments;
		segments.resize(count - 1);
		for (size_t i = 0; i + 1 < count; ++i) {
			segments[i] = (kept[i + 1]->position - kept[i]->position).Normalized();
		}

		Emitter emit(out, shape.textureScale);
		Vector<Joint>& joints = scratch.joints;
		joints.resize(count);
		Vec3 carried = AnyPerpendicular(FacingAt(facing, kept[0]->position));
		for (size_t j = 0; j < count; ++j) {
			const RibbonPoint& point = *kept[j];
			const Vec3 normal = FacingAt(facing, point.position);
			const Vec3 into = j > 0 ? segments[j - 1] : (closed ? segments[count - 2] : segments[0]);
			const Vec3 onward = j + 1 < count ? segments[j] : (closed ? segments[0] : segments[count - 2]);
			Vec3 average = into + onward;
			average = average.LengthSquared() > 1e-8f ? average.Normalized() : into;
			const Vec3 side = SideOf(average, normal, carried);
			carried = side;
			const Vec3 tangent = Vec3::Cross(side, normal).Normalized();
			const f32 half = point.width * 0.5f;
			const Vec3 origin = point.position - normal * (facing.shadowBias * point.width);
			const Color32 color = point.color.ToColor32();
			const MeshVertex style = emit.Make(origin, normal, tangent, point.u, 0.5f, color);

			const bool interior = closed || (j > 0 && j + 1 < count);
			Joint& joint = joints[j];
			if (shape.cornerVertices == 0 || !interior || Vec3::Dot(into, onward) > STRAIGHT_COSINE) {
				joint.inA = joint.outA = emit.Make(origin - side * half, normal, tangent, point.u, 1.0f, color);
				joint.inB = joint.outB = emit.Make(origin + side * half, normal, tangent, point.u, 0.0f, color);
			} else {
				const f32 inner = Vec3::Dot(Vec3::Cross(into, onward), normal) >= 0.0f ? 1.0f : -1.0f;
				const Vec3 outerFrom = -SideOf(into, normal, side) * inner;
				const Vec3 outerTo = -SideOf(onward, normal, side) * inner;
				const MeshVertex innerVertex = emit.Make(origin + side * (inner * half), normal, tangent, point.u, inner > 0.0f ? 0.0f : 1.0f, color);
				const MeshVertex outerIn = emit.Make(origin + outerFrom * half, normal, tangent, point.u, inner > 0.0f ? 1.0f : 0.0f, color);
				const MeshVertex outerOut = emit.Make(origin + outerTo * half, normal, tangent, point.u, inner > 0.0f ? 1.0f : 0.0f, color);
				(inner > 0.0f ? joint.inB : joint.inA) = innerVertex;
				(inner > 0.0f ? joint.outB : joint.outA) = innerVertex;
				(inner > 0.0f ? joint.inA : joint.inB) = outerIn;
				(inner > 0.0f ? joint.outA : joint.outB) = outerOut;
				if (!closed || j > 0) {
					const f32 sweep = std::atan2(Vec3::Dot(Vec3::Cross(outerFrom, outerTo), normal), Vec3::Dot(outerFrom, outerTo));
					emit.Fan(innerVertex, origin, outerFrom, sweep, shape.cornerVertices + 1, half, normal, side, style);
				}
			}

			if (shape.endCapVertices > 0 && !closed && (j == 0 || j + 1 == count)) {
				const f32 sweep = j == 0 ? Math::PI : -Math::PI;
				emit.Fan(style, origin, side, sweep, shape.endCapVertices + 1, half, normal, side, style);
			}
		}

		for (size_t j = 0; j + 1 < count; ++j) {
			const Joint& from = joints[j];
			const Joint& to = joints[j + 1];
			emit.Triangle(from.outA, to.inA, to.inB);
			emit.Triangle(from.outA, to.inB, from.outB);
		}
	}

	AABB RibbonBounds(std::span<const RibbonPoint> points) {
		AABB bounds = AABB::Empty();
		for (const RibbonPoint& point : points) {
			const Vec3 reach(point.width, point.width, point.width);
			bounds.Encapsulate(point.position - reach);
			bounds.Encapsulate(point.position + reach);
		}
		return bounds;
	}

	void DeclareRibbon(ProceduralPlan& plan, std::span<const RibbonPoint> points, std::span<const Handle<MaterialAsset>> materials,
					   ShadowCasting castShadows) {
		if (points.size() < 2) {
			return;
		}
		const AABB bounds = RibbonBounds(points);
		plan.SetBounds(bounds);
		for (const Handle<MaterialAsset>& material : materials) {
			if (material.IsValid()) {
				plan.AddItem(material, bounds.Center(), castShadows);
			}
		}
	}

	void RecordRibbon(ProceduralContext& context, std::span<const RibbonPoint> points, const RibbonShape& shape, LineAlignment alignment,
					  const Vec3& transformZ, f32 shadowBias, bool receiveShadows) {
		RibbonFacing facing = FacingFor(alignment, transformZ, context.GetView(), context.GetProjection(), context.GetCameraPosition());
		if (context.GetPass() == ProceduralPass::Shadow) {
			facing.shadowBias = shadowBias;
		}
		Vector<MeshVertex>& vertices = Scratched().vertices;
		BuildRibbon(points, shape, facing, vertices);
		if (vertices.size() >= 3) {
			context.DrawTriangles(vertices, receiveShadows);
		}
	}

	void ShutdownRibbons() {
		if (_scratch) {
			Delete(_scratch);
			_scratch = nullptr;
		}
	}

} // namespace Shalltear::VisualEffectsImpl
