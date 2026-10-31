#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "EffectKernelHarness.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <algorithm>
#include <format>
#include <numeric>

using namespace Shalltear;
using namespace Shalltear::EffectKernelHarness;

namespace {

	using VisualEffectsImpl::ParticleOutputDesc;
	using VisualEffectsImpl::ParticleSort;
	using VisualEffectsImpl::ParticleSortStep;

	class SortProbeRenderer : public ProceduralRenderer {
	public:
		SLT_REFLECT_COMPONENT(SortProbeRenderer, ProceduralRenderer)
		SLT_END_REFLECT()

		ParticleSystem* system = nullptr;
		Vector<ParticleSortStep> steps{GetMemoryResource(MemTag::Graphics)};
		u32 viewSteps = 0;

		void Declare(ProceduralPlan& plan) override {
			steps.clear();
			if (!system) {
				return;
			}
			plan.SetBounds(AABB::FromCenterExtents(Vec3{}, Vec3(10.0f, 10.0f, 10.0f)));
			system->DeclareOutput(plan, Vec3{}, 0);
			system->DeclareViewSorts(plan, steps);
		}

		void Record(ProceduralContext& context) override { system->RecordOutput(context, 0); }

		void RecordView(ComputeContext& context, const ProceduralView& view) override {
			++viewSteps;
			const ParticleSortStep& step = steps[context.GetStep()];
			const Mat4 inverse = view.view.Inverse();
			system->RecordSort(context, step.output, step.step, view.cameraPosition, Vec3(inverse(0, 2), inverse(1, 2), inverse(2, 2)).Normalized());
		}
	};

	SLT_REGISTER_TYPE(SortProbeRenderer)

	struct SortRun {
		GraphEffect effect;
		ShaderAsset initialize;
		ShaderAsset update;
		ShaderAsset key;
		ShaderAsset output;
		MaterialAsset material;
		ParticleSystem system;
	};

	str SortedEffect(u32 capacity, std::string_view initialize, std::string_view settings, std::string_view in = "{}", std::string_view nodes = "") {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":{}}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"settings":{},"in":{},"blocks":[]}}]}})",
						   nodes, capacity, initialize, settings, in);
	}

	constexpr std::string_view RANDOM_SIZE = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size","Random":"Uniform"},"in":{"A":{"value":[0]},"B":{"value":[1]}}})";
	constexpr std::string_view SIZE_NODE = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"size"}})";
	constexpr std::string_view SIZE_KEY = R"({"Sort Key":{"from":[20,"size"]}})";
	constexpr std::string_view RANDOM_POSITION =
		R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position","Random":"PerComponent"},"in":{"A":{"value":[-2,-2,-2]},"B":{"value":[2,2,2]}}})";

	ViewDesc ViewFrom(f32 z) {
		ViewDesc desc;
		desc.cameraPosition = Vec3(0.0f, 0.0f, z);
		desc.view = Mat4::LookAt(desc.cameraPosition, Vec3{});
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.clearColor = Color::Black();
		return desc;
	}

} // anonymous namespace

class ParticleSortFixture : public GraphEffectBlockFixture {
protected:
	static constexpr f32 DELTA = 0.1f;

	bool BeginSorted(SortRun& run, const str& text, std::string_view name) {
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		if (!GraphEffectTarget::Compile(ParseEffect(text), name, run.effect, diagnostics)) {
			ADD_FAILURE() << name << " doesn't compile: " << (diagnostics.empty() ? str() : diagnostics[0].message);
			return false;
		}
		const GraphEffectSystem& compiled = run.effect.systems[0];
		const GraphEffectOutput& output = compiled.outputs[0];
		if (output.sort == GraphEffectSort::None) {
			ADD_FAILURE() << name << "'s output doesn't sort";
			return false;
		}
		if (!Import(run.initialize, compiled.initializeKernel, (str(name) + ".Initialize.hlsl").c_str()) ||
			!Import(run.update, compiled.updateKernel, (str(name) + ".Update.hlsl").c_str()) ||
			!Import(run.key, output.sortKernel, (str(name) + ".Sort.hlsl").c_str()) || !Import(run.output, output.shader, (str(name) + ".Output.hlsl").c_str())) {
			return false;
		}
		run.material.shader = Handle<ShaderAsset>(&run.output);
		ParticleSystemDesc desc;
		desc.capacity = compiled.capacity;
		desc.strips = compiled.strips;
		desc.attributeCount = compiled.attributeCount;
		desc.initialize = Handle<ShaderAsset>(&run.initialize);
		desc.update = Handle<ShaderAsset>(&run.update);
		desc.world = compiled.world;
		ParticleOutputDesc drawn;
		drawn.material = Handle<MaterialAsset>(&run.material);
		drawn.vertices = output.vertices;
		drawn.strip = output.strip;
		const bool perView = output.sort == GraphEffectSort::DistanceToCamera || output.sort == GraphEffectSort::CameraDepth;
		drawn.sort = perView ? ParticleSort::View : ParticleSort::Frame;
		drawn.sortSign = output.revertSorting ? -1.0f : 1.0f;
		drawn.sortKernel = Handle<ShaderAsset>(&run.key);
		desc.outputs.push_back(drawn);
		return run.system.Create(desc);
	}

	static void Step(SortRun& run, u32 frame, u32 spawn, std::initializer_list<std::pair<ViewTargetId, ViewDesc>> views = {}) {
		const Vector<u8> block = Frame(run.effect, static_cast<f32>(frame) * DELTA, DELTA, spawn, 7);
		run.system.SetFrameData(block);
		run.system.Advance(DELTA, spawn);
		for (const auto& [target, desc] : views) {
			RenderSystem::SubmitView(target, desc);
		}
		RenderSystem::RenderAll();
	}

	static Vector<u32> Order(const SortRun& run, Vector<u32>& alive) {
		GraphEffectKernelFixture::Alive(run.system, alive);
		Vector<u32> order(alive.size(), 0, GetMemoryResource(MemTag::Graphics));
		if (!order.empty()) {
			RenderSystem::GetDevice()->ReadbackBuffer(run.system.GetOrderBuffer(), {reinterpret_cast<u8*>(order.data()), order.size() * 4},
													  static_cast<u64>(run.system.GetOrderOffset(0)) * 4);
		}
		return order;
	}

	static Vector<f32> Stored(const SortRun& run, std::string_view attribute, u32 component) {
		Vector<f32> column(run.system.GetCapacity(), 0.0f, GetMemoryResource(MemTag::Graphics));
		for (const GraphEffectStored& stored : run.effect.systems[0].stored) {
			if (stored.attribute == attribute) {
				RenderSystem::GetDevice()->ReadbackBuffer(run.system.GetAttributesBuffer(), {reinterpret_cast<u8*>(column.data()), column.size() * 4},
														  run.system.GetAttributesOffset() + static_cast<u64>(stored.slot + component) * run.system.GetCapacity() * 4);
				return column;
			}
		}
		ADD_FAILURE() << attribute << " is not stored";
		return column;
	}

	template <typename Key>
	static Vector<u32> Expected(const Vector<u32>& alive, Key&& key) {
		Vector<u32> order(alive.begin(), alive.end(), GetMemoryResource(MemTag::Graphics));
		std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
			const f32 ka = key(a);
			const f32 kb = key(b);
			return ka != kb ? ka > kb : a < b;
		});
		return order;
	}

	template <typename Key>
	static ::testing::AssertionResult Descending(const Vector<u32>& order, const Vector<u32>& alive, Key&& key, f32 tolerance) {
		Vector<u32> a(order.begin(), order.end(), GetMemoryResource(MemTag::Graphics));
		Vector<u32> b(alive.begin(), alive.end(), GetMemoryResource(MemTag::Graphics));
		std::sort(a.begin(), a.end());
		std::sort(b.begin(), b.end());
		if (a != b) {
			return ::testing::AssertionFailure() << "order isn't the alive particles reordered";
		}
		for (size_t i = 1; i < order.size(); ++i) {
			if (key(order[i]) > key(order[i - 1]) + tolerance) {
				return ::testing::AssertionFailure() << "slot " << i << " keys " << key(order[i]) << " after " << key(order[i - 1]);
			}
		}
		return ::testing::AssertionSuccess();
	}
};

TEST_F(ParticleSortFixture, CustomKeySortsAscending) {
	for (const u32 capacity : {50u, 1024u, 3000u}) {
		for (const bool revert : {false, true}) {
			SCOPED_TRACE(std::format("capacity {}, revert {}", capacity, revert));
			SortRun run;
			const str settings = std::format(R"({{"Sort Mode":"Custom","Revert Sorting":{}}})", revert ? "true" : "false");
			if (!BeginSorted(run, SortedEffect(capacity, RANDOM_SIZE, settings, SIZE_KEY, SIZE_NODE), "Custom")) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
			Step(run, 0, capacity);
			Vector<u32> alive{GetMemoryResource(MemTag::Graphics)};
			const Vector<u32> order = Order(run, alive);
			ASSERT_EQ(alive.size(), capacity);
			const f32 sign = revert ? -1.0f : 1.0f;
			const Vector<f32> size = Stored(run, "size", 0);
			EXPECT_EQ(order, Expected(alive, [&](u32 index) { return -size[index] * sign; }));
		}
	}
}

TEST_F(ParticleSortFixture, YoungestInFrontDrawsTheOldestFirst) {
	SortRun run;
	if (!BeginSorted(run, SortedEffect(64, RANDOM_SIZE, R"({"Sort Mode":"YoungestInFront"})"), "Youngest")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	for (u32 frame = 0; frame < 3; ++frame) {
		Step(run, frame, 5);
	}
	Vector<u32> alive{GetMemoryResource(MemTag::Graphics)};
	const Vector<u32> order = Order(run, alive);
	ASSERT_EQ(alive.size(), 15u);
	const Vector<f32> ages = Stored(run, "age", 0);
	const auto age = [&](u32 index) { return ages[index]; };
	EXPECT_EQ(order, Expected(alive, age));
	EXPECT_GT(age(order.front()), age(order.back()));
}

TEST_F(ParticleSortFixture, CameraKeySortedPerView) {
	for (const char* mode : {"DistanceToCamera", "CameraDepth"}) {
		SCOPED_TRACE(mode);
		SortRun run;
		if (!BeginSorted(run, SortedEffect(200, RANDOM_POSITION, std::format(R"({{"Sort Mode":"{}"}})", mode)), mode)) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Handle<Scene> scene = SceneSystem::CreateScene("Sort");
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", scene);
		Handle<SortProbeRenderer> renderer = object->AddComponent<SortProbeRenderer>();
		renderer->system = &run.system;
		SceneSystem::EndFrame();
		const ViewTargetId front = RenderSystem::CreateViewTarget({64, 64});
		const ViewTargetId back = RenderSystem::CreateViewTarget({64, 64});
		const ViewDesc fromFront = ViewFrom(6.0f);
		const ViewDesc fromBack = ViewFrom(-6.0f);
		const bool depth = str(mode) == "CameraDepth";
		Vector<f32> positions[3] = {Vector<f32>{GetMemoryResource(MemTag::Graphics)}, Vector<f32>{GetMemoryResource(MemTag::Graphics)},
									Vector<f32>{GetMemoryResource(MemTag::Graphics)}};
		const auto keyFrom = [&](const ViewDesc& view, bool alongView) {
			const Vec3 eye = view.cameraPosition;
			const Vec3 forward = (Vec3{} - eye).Normalized();
			return [&positions, eye, forward, alongView](u32 index) {
				const Vec3 to = Vec3(positions[0][index], positions[1][index], positions[2][index]) - eye;
				return alongView ? Vec3::Dot(to, forward) : Vec3::Dot(to, to);
			};
		};

		Step(run, 0, 200, {{front, fromFront}});
		Vector<u32> alive{GetMemoryResource(MemTag::Graphics)};
		Vector<u32> order = Order(run, alive);
		ASSERT_EQ(alive.size(), 200u);
		for (u32 component = 0; component < 3; ++component) {
			positions[component] = Stored(run, "position", component);
		}
		EXPECT_TRUE(Descending(order, alive, keyFrom(fromFront, depth), 1.0e-4f));
		EXPECT_FALSE(Descending(order, alive, keyFrom(fromFront, !depth), 1.0e-4f));
		const u32 steps = renderer->viewSteps;
		EXPECT_GT(steps, 0u);

		Step(run, 1, 0, {{back, fromBack}});
		order = Order(run, alive);
		EXPECT_TRUE(Descending(order, alive, keyFrom(fromBack, depth), 1.0e-4f));
		EXPECT_EQ(renderer->viewSteps, 2 * steps);

		Step(run, 2, 0, {{front, fromFront}, {back, fromBack}});
		order = Order(run, alive);
		EXPECT_TRUE(Descending(order, alive, keyFrom(fromBack, depth), 1.0e-4f));
		EXPECT_EQ(renderer->viewSteps, 4 * steps);

		Step(run, 3, 0);
		EXPECT_EQ(renderer->viewSteps, 4 * steps);

		RenderSystem::DestroyViewTarget(front);
		RenderSystem::DestroyViewTarget(back);
		renderer->system = nullptr;
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();
	}
}

TEST_F(ParticleSortFixture, StripOutputSortsSegmentsDeadAmongThem) {
	const str nodes = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleIndexInStrip"}},
		{"id":21,"def":"Math/Subtract","in":{"A":{"from":[20,"particleIndexInStrip"]},"B":{"value":[2,0,0,0]}}},
		{"id":22,"def":"Math/Sign","in":{"In":{"from":[21,"Out"]}}},
		{"id":23,"def":"Math/Absolute","in":{"In":{"from":[22,"Out"]}}},
		{"id":24,"def":"Math/Multiply","in":{"A":{"from":[23,"Out"]},"B":{"value":[10,0,0,0]}}},
		{"id":25,"def":"Math/Add","in":{"A":{"from":[24,"Out"]},"B":{"value":[0.05,0,0,0]}}})";
	const str text = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
		{{"id":1,"def":"Context/Initialize","settings":{{"Data Type":"ParticleStrip","Strip Capacity":1,"Particle Per Strip Count":8}},"blocks":[
			{{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"lifetime"}},"in":{{"Lifetime":{{"from":[25,"Out"]}}}}}}]}},
		{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[]}},
		{{"id":3,"def":"Context/OutputParticleStripQuad","flow":[[2,"In"]],"settings":{{"Sort Mode":"Custom"}},
			"in":{{"Sort Key":{{"from":[20,"particleIndexInStrip"]}}}},"blocks":[]}}]}})",
								 nodes);
	SortRun run;
	if (!BeginSorted(run, text, "StripSorted")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const auto entries = [&]() {
		Vector<u32> order(run.system.GetCapacity(), 0, GetMemoryResource(MemTag::Graphics));
		RenderSystem::GetDevice()->WaitIdle();
		RenderSystem::GetDevice()->ReadbackBuffer(run.system.GetOrderBuffer(), {reinterpret_cast<u8*>(order.data()), order.size() * 4},
												  static_cast<u64>(run.system.GetOrderOffset(0)) * 4);
		return order;
	};
	const Vector<u32> segments{{0, 1, 2, 3, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}, GetMemoryResource(MemTag::Graphics)};
	Step(run, 0, 5);
	EXPECT_EQ(entries(), segments);
	Step(run, 1, 0);
	Vector<u32> alive{GetMemoryResource(MemTag::Graphics)};
	GraphEffectKernelFixture::Alive(run.system, alive);
	EXPECT_EQ(alive.size(), 4u);
	EXPECT_EQ(entries(), segments);
}

#endif
