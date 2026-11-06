#include "Shalltear/VisualEffects/TrailRenderer.h"

#include "Ribbon.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include <algorithm>

SLT_REGISTER_TYPE(Shalltear::TrailRenderer)

namespace Shalltear {

	namespace VisualEffectsImpl {

		struct TrailPoint {
			Vec3 position{};
			f32 birth = 0.0f;
		};

		struct TrailState {
			Vector<TrailPoint> emitted{GetMemoryResource(MemTag::Graphics)};
			RibbonState ribbon;
			f32 clock = 0.0f;
			f32 still = 0.0f;
			Vec3 last{};
			bool placed = false;
		};

		struct TrailAccess {
			static TrailState* State(TrailRenderer& trail) { return trail._trail; }
		};

		namespace {

			constexpr f32 STILL_DISTANCE_SQUARED = 1e-10f;

			Vector<Handle<TrailRenderer>>* _trails = nullptr;

			Vector<Handle<TrailRenderer>>& Trails() {
				if (!_trails) {
					_trails = New<Vector<Handle<TrailRenderer>>>(MemTag::Graphics, GetMemoryResource(MemTag::Graphics));
				}
				return *_trails;
			}

			bool Expired(const TrailState& state, const TrailPoint& point, f32 lifetime) { return state.clock - point.birth >= lifetime; }

			void Advance(TrailRenderer& trail, TrailState& state, f32 deltaTime) {
				SceneObject* owner = trail.GetOwner().Get();
				if (!owner) {
					return;
				}
				const Vec3 head = owner->GetWorldPosition();
				state.clock += deltaTime;
				Vector<TrailPoint>& emitted = state.emitted;
				size_t drop = 0;
				while (drop + 1 < emitted.size() && Expired(state, emitted[drop + 1], trail.time)) {
					++drop;
				}
				emitted.erase(emitted.begin(), emitted.begin() + static_cast<std::ptrdiff_t>(drop));
				if (emitted.size() == 1 && Expired(state, emitted[0], trail.time)) {
					emitted.clear();
				}
				const f32 spacing = Math::Max(trail.minVertexDistance, 0.0f);
				if (trail.emitting && (emitted.empty() || Vec3::DistanceSquared(head, emitted.back().position) >= spacing * spacing)) {
					emitted.push_back({head, state.clock});
				}

				const bool moved = !state.placed || Vec3::DistanceSquared(head, state.last) > STILL_DISTANCE_SQUARED;
				state.still = moved ? 0.0f : state.still + deltaTime;
				state.last = head;
				state.placed = true;
				if (trail.autodestruct && state.still >= trail.time) {
					SceneSystem::Destroy(trail.GetOwner());
				}
			}

		} // anonymous namespace

		void AdvanceTrails(f32 deltaTime) {
			if (!_trails) {
				return;
			}
			std::erase_if(*_trails, [](const Handle<TrailRenderer>& handle) { return handle.Get() == nullptr; });
			const Vector<Handle<TrailRenderer>> trails = *_trails;
			for (const Handle<TrailRenderer>& handle : trails) {
				TrailRenderer* trail = handle.Get();
				TrailState* state = trail ? TrailAccess::State(*trail) : nullptr;
				if (state) {
					Advance(*trail, *state, deltaTime);
				}
			}
		}

		void ShutdownTrails() {
			if (_trails) {
				Delete(_trails);
				_trails = nullptr;
			}
		}

	} // namespace VisualEffectsImpl

	using namespace VisualEffectsImpl;

	TrailRenderer::~TrailRenderer() {
		if (_trail) {
			Delete(_trail);
			_trail = nullptr;
		}
	}

	void TrailRenderer::OnEnable() {
		ProceduralRenderer::OnEnable();
		if (!_trail) {
			_trail = New<TrailState>(MemTag::Graphics);
		}
		Vector<Handle<TrailRenderer>>& trails = Trails();
		const Handle<TrailRenderer> handle(this);
		if (std::find(trails.begin(), trails.end(), handle) == trails.end()) {
			trails.push_back(handle);
		}
	}

	void TrailRenderer::OnDisable() {
		if (_trails) {
			std::erase(*_trails, Handle<TrailRenderer>(this));
		}
		if (_trail) {
			Delete(_trail);
			_trail = nullptr;
		}
		ProceduralRenderer::OnDisable();
	}

	void TrailRenderer::AddPosition(const Vec3& position) {
		if (_trail) {
			_trail->emitted.push_back({position, _trail->clock});
		}
	}

	void TrailRenderer::Clear() {
		if (_trail) {
			_trail->emitted.clear();
		}
	}

	u32 TrailRenderer::GetPositionCount() const {
		if (!_trail) {
			return 0;
		}
		const TrailState& state = *_trail;
		return static_cast<u32>(std::count_if(state.emitted.begin(), state.emitted.end(),
											  [&](const TrailPoint& point) { return !Expired(state, point, time); }));
	}

	Vec3 TrailRenderer::GetPosition(u32 index) const {
		if (!_trail) {
			return Vec3{};
		}
		u32 alive = 0;
		for (const TrailPoint& point : _trail->emitted) {
			if (!Expired(*_trail, point, time) && alive++ == index) {
				return point.position;
			}
		}
		return Vec3{};
	}

	void TrailRenderer::Declare(ProceduralPlan& plan) {
		const SceneObject* owner = GetOwner().Get();
		if (!_trail || !owner) {
			return;
		}
		TrailState& state = *_trail;
		RibbonState& ribbon = state.ribbon;
		ribbon.positions.clear();
		ribbon.t.clear();
		ribbon.points.clear();
		const Mat4& world = owner->GetWorldMatrix();
		ribbon.transformZ = Vec3(world(0, 2), world(1, 2), world(2, 2));
		const f32 lifetime = Math::Max(time, 1e-6f);
		if (emitting && !state.emitted.empty()) {
			ribbon.positions.push_back(owner->GetWorldPosition());
			ribbon.t.push_back(0.0f);
		}
		for (size_t i = state.emitted.size(); i-- > 0;) {
			const TrailPoint& point = state.emitted[i];
			const f32 age = state.clock - point.birth;
			if (age < lifetime) {
				ribbon.positions.push_back(point.position);
				ribbon.t.push_back(age / lifetime);
				continue;
			}
			if (i + 1 < state.emitted.size()) {
				const TrailPoint& younger = state.emitted[i + 1];
				const f32 youngerAge = state.clock - younger.birth;
				const f32 span = age - youngerAge;
				const f32 reach = span > 0.0f ? (lifetime - youngerAge) / span : 0.0f;
				ribbon.positions.push_back(Vec3::Lerp(younger.position, point.position, reach));
				ribbon.t.push_back(1.0f);
			}
			break;
		}
		const RibbonStyle style{&width, widthMultiplier, &color, textureMode};
		FillRibbonPoints(ribbon.positions, ribbon.t, style, false, ribbon.points);
		DeclareRibbon(plan, ribbon.points, materials, castShadows);
	}

	void TrailRenderer::Record(ProceduralContext& context) {
		if (!_trail) {
			return;
		}
		const RibbonShape shape{cornerVertices, endCapVertices, false, textureScale};
		RecordRibbon(context, _trail->ribbon.points, shape, alignment, _trail->ribbon.transformZ, shadowBias, receiveShadows);
	}

} // namespace Shalltear
