#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "GraphEffect.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectListener.h"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <functional>

#ifndef SHALLTEAR_SHARED
#include "EffectAsset.h"
#include "EffectInstance.h"
#include "EffectPass.h"
#include "ParticleSimulation.h"
#endif

using namespace Shalltear;
using namespace ShadingTest;

#ifndef SHALLTEAR_SHARED

SLT_DECLARE_LINKED_PACKAGE(VisualEffects)

namespace {

	using namespace VisualEffectsImpl;

	EffectProgram AssembleOrFail(std::string_view text) {
		EffectProgram program;
		str error;
		EXPECT_TRUE(EffectPrograms::Assemble(text, program, error)) << error;
		return program;
	}

	str Bits(f32 value) {
		u32 bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return std::format("0x{:08X}", bits);
	}

	class EffectEventRecorder : public Behavior, public VisualEffectListener {
	public:
		SLT_REFLECT_COMPONENT(EffectEventRecorder, Behavior)
		SLT_END_REFLECT()

		struct Heard {
			str name;
			Vec4 position{};
			Vec4 id{};
			Vec4 count{};
			Vec4 color{};
		};
		Vector<Heard> heard{GetMemoryResource(MemTag::Scene)};
		bool reinitOnFirst = false;

		void OnVisualEffectOutputEvent(VisualEffect& effect, std::string_view name, const VisualEffectEventAttributes& attributes) override {
			Heard one;
			one.name = str(name);
			attributes.Find("position", one.position);
			attributes.Find("particleId", one.id);
			attributes.Find("spawnCount", one.count);
			attributes.Find("color", one.color);
			heard.push_back(std::move(one));
			if (reinitOnFirst && heard.size() == 1) {
				effect.Reinit();
			}
		}
	};

	class VisualEffectComponentFixture : public MaterialReadbackFixture {
	protected:
		char _token = 0;
		bool _ready = false;
		std::filesystem::path _fixtures;
		Vector<VisualEffectAsset*> _assets{GetMemoryResource(MemTag::Asset)};
		Handle<Scene> _scene;
		EffectTiming _timing;
		EffectInstancing _instancing;

		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			if (!RenderSystem::IsRenderingAvailable()) {
				return;
			}
			SLT_CALL_LINKED_PACKAGE(VisualEffects, &_token);
			_fixtures = RepoDir("Source/Tests/Fixtures/VisualEffects");
			_scene = SceneSystem::CreateScene("Effects");
			_ready = !_fixtures.empty() && _scene.IsValid();
		}

		void TearDown() override {
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			RenderSystem::RenderAll();
			for (VisualEffectAsset* asset : _assets) {
				Delete(asset);
			}
			_assets.clear();
			ShutdownEffects();
			ShutdownSimulation();
			FramePasses::UnregisterOwnedBy(&_token);
			FrameStages::UnregisterOwnedBy(&_token);
			AssetImporterRegistry::UnregisterOwnedBy(&_token);
			MaterialReadbackFixture::TearDown();
		}

		ShaderAsset* Import(const char* file) {
			ShaderAsset* shader = New<ShaderAsset>(MemTag::Asset);
			if (!shader->Import(Path((_fixtures / file).generic_string()))) {
				Delete(shader);
				return nullptr;
			}
			return shader;
		}

		VisualEffectAsset* MakeAsset(EffectSpawnKind kind, std::string_view spawn, bool source = false,
									 std::initializer_list<const char*> start = {"OnPlay"}) {
			return MakeAsset(kind, spawn, source ? "SourceInitialize.hlsl" : "AgeInitialize.hlsl", source ? 5u : 2u, source, start);
		}

		VisualEffectAsset* MakeAsset(EffectSpawnKind kind, std::string_view spawn, const char* kernel, u32 attributes, bool source = false,
									 std::initializer_list<const char*> start = {"OnPlay"}, std::initializer_list<const char*> linked = {},
									 const std::function<void(EffectDescription&)>& shape = {}) {
			ShaderAsset* initialize = Import(kernel);
			ShaderAsset* update = Import("AgeUpdate.hlsl");
			if (!initialize || !update) {
				Delete(initialize);
				Delete(update);
				return nullptr;
			}
			EffectDescription description;
			description.timing = _timing;
			description.instancing = _instancing;
			EffectProperty lifetime;
			lifetime.name = "Lifetime";
			lifetime.reference = "lifetime";
			lifetime.offset = 96;
			lifetime.defaultValue[0] = 100.0f;
			description.properties.push_back(lifetime);
			description.batchesOffset = 112;
			EffectSystem system;
			if (!spawn.empty()) {
				EffectSpawner spawner;
				for (const char* name : start) {
					spawner.start.push_back(name);
					description.events.push_back(name);
				}
				spawner.stop.push_back("OnStop");
				description.events.push_back("OnStop");
				EffectSpawnBlock block;
				block.kind = kind;
				block.program = AssembleOrFail(spawn);
				spawner.blocks.push_back(std::move(block));
				description.spawners.push_back(std::move(spawner));
				system.spawners.push_back(0);
			}
			for (const char* name : linked) {
				system.events.push_back(name);
				description.events.push_back(name);
			}
			system.capacity = 64;
			system.attributeCount = attributes;
			system.initialize = 0;
			system.update = 1;
			system.bounds = AssembleOrFail("const 3 0x00000000 0x00000000 0x00000000\nconst 3 0x40000000 0x40000000 0x40000000\nroot 0\nroot 1\nroot 0\n");
			if (source) {
				system.source.push_back({"position", 4, 3, {0.0f, 0.0f, 0.0f, 0.0f}});
				system.batchStride = 16;
			}
			description.systems.push_back(std::move(system));
			if (shape) {
				shape(description);
			}
			VisualEffectAsset* asset = New<VisualEffectAsset>(MemTag::Asset);
			Vector<ShaderAsset*> shaders{{initialize, update}, GetMemoryResource(MemTag::Asset)};
			if (!EffectAssetAccess::Adopt(*asset, std::move(description), shaders)) {
				Delete(asset);
				return nullptr;
			}
			_assets.push_back(asset);
			return asset;
		}

		VisualEffectAsset* MakeReadAsset(f32 count) {
			ShaderAsset* initialize = Import("AgeInitialize.hlsl");
			ShaderAsset* update = Import("AgeUpdate.hlsl");
			if (!initialize || !update) {
				Delete(initialize);
				Delete(update);
				return nullptr;
			}
			EffectDescription description;
			description.timing = _timing;
			description.instancing = _instancing;
			EffectProperty lifetime;
			lifetime.name = "Lifetime";
			lifetime.reference = "lifetime";
			lifetime.offset = 96;
			lifetime.defaultValue[0] = 100.0f;
			description.properties.push_back(lifetime);
			description.batchesOffset = 112;
			description.events = {"OnPlay", "OnStop"};
			EffectSpawner spawner;
			spawner.start.push_back("OnPlay");
			spawner.stop.push_back("OnStop");
			EffectSpawnBlock block;
			block.kind = EffectSpawnKind::SingleBurst;
			block.program = AssembleOrFail(Burst(count));
			spawner.blocks.push_back(std::move(block));
			description.spawners.push_back(std::move(spawner));
			const EffectProgram bounds =
				AssembleOrFail("const 3 0x00000000 0x00000000 0x00000000\nconst 3 0x40000000 0x40000000 0x40000000\nroot 0\nroot 1\nroot 0\n");

			EffectSystem sparks;
			sparks.name = "Sparks";
			sparks.cpu = true;
			sparks.capacity = 16;
			sparks.attributeCount = 4;
			sparks.stored.push_back({"position", 0, 3});
			sparks.stored.push_back({"particleId", 3, 1});
			sparks.initialize = EFFECT_NO_BINDING;
			sparks.update = EFFECT_NO_BINDING;
			sparks.initializeProgram = AssembleOrFail("input 1 Attribute/spawnIndex\ninput 1 Attribute/particleId\nconst 1 0x3F800000\n"
													  "const 1 0x00000000\nconstruct 3 0 0 3 3\nroot 2\nroot 4\nroot 1\n");
			sparks.updateProgram = AssembleOrFail("input 3 Attribute/position\ninput 4 TimeParameters\nswizzle 1 49 1\nconst 1 0x00000000\n"
												  "construct 3 0 3 2 3\nadd 3 0 0 4\nconst 1 0x3F800000\ninput 1 Attribute/particleId\n"
												  "root 6\nroot 5\nroot 7\nroot 6\n");
			sparks.updateOutputEvents.push_back({"Rose", 3});
			sparks.updateCarried.push_back({"position", 1, 3});
			sparks.updateCarried.push_back({"particleId", 2, 1});
			sparks.bounds = bounds;
			sparks.spawners.push_back(0);
			description.systems.push_back(std::move(sparks));

			EffectSystem smoke;
			smoke.name = "Smoke";
			smoke.capacity = 64;
			smoke.attributeCount = 2;
			smoke.initialize = 0;
			smoke.update = 1;
			smoke.bounds = bounds;
			smoke.spawners.push_back(0);
			description.systems.push_back(std::move(smoke));

			VisualEffectAsset* asset = New<VisualEffectAsset>(MemTag::Asset);
			Vector<ShaderAsset*> shaders{{initialize, update}, GetMemoryResource(MemTag::Asset)};
			if (!EffectAssetAccess::Adopt(*asset, std::move(description), shaders)) {
				Delete(asset);
				return nullptr;
			}
			_assets.push_back(asset);
			return asset;
		}

		VisualEffectAsset* MakeSeenAsset(std::string_view spawn, std::initializer_list<const char*> linked = {}) {
			ShaderAsset* initialize = Import("AgeInitialize.hlsl");
			ShaderAsset* update = Import("AgeUpdate.hlsl");
			ShaderAsset* output = Import("QuadOutput.hlsl");
			if (!initialize || !update || !output) {
				Delete(initialize);
				Delete(update);
				Delete(output);
				return nullptr;
			}
			EffectDescription description;
			description.timing = _timing;
			description.instancing = _instancing;
			EffectProperty lifetime;
			lifetime.name = "Lifetime";
			lifetime.reference = "lifetime";
			lifetime.offset = 96;
			lifetime.defaultValue[0] = 100.0f;
			description.properties.push_back(lifetime);
			EffectProperty center;
			center.name = "Center";
			center.reference = "center";
			center.offset = 100;
			center.components = 3;
			description.properties.push_back(center);
			description.batchesOffset = 112;
			description.events = {"OnPlay", "OnStop"};
			EffectSpawner spawner;
			spawner.start.push_back("OnPlay");
			spawner.stop.push_back("OnStop");
			EffectSpawnBlock block;
			block.kind = EffectSpawnKind::ConstantRate;
			block.program = AssembleOrFail(spawn);
			spawner.blocks.push_back(std::move(block));
			description.spawners.push_back(std::move(spawner));
			EffectSystem system;
			system.capacity = 64;
			system.attributeCount = 2;
			system.stored.push_back({"age", 0, 1});
			system.stored.push_back({"lifetime", 1, 1});
			system.initialize = 0;
			system.update = 1;
			system.outputs.push_back({2, 6});
			system.spawners.push_back(0);
			for (const char* name : linked) {
				system.events.push_back(name);
				description.events.push_back(name);
			}
			system.bounds = AssembleOrFail("input 3 center\nconst 3 0x40000000 0x40000000 0x40000000\nconst 3 0x00000000 0x00000000 0x00000000\nroot 0\nroot 1\nroot 2\n");
			description.systems.push_back(std::move(system));
			VisualEffectAsset* asset = New<VisualEffectAsset>(MemTag::Asset);
			Vector<ShaderAsset*> shaders{{initialize, update, output}, GetMemoryResource(MemTag::Asset)};
			if (!EffectAssetAccess::Adopt(*asset, std::move(description), shaders)) {
				Delete(asset);
				return nullptr;
			}
			_assets.push_back(asset);
			return asset;
		}

		VisualEffectAsset* MakeBoundedAsset(EffectBoundsMode mode) {
			ShaderAsset* initialize = Import("AgeInitialize.hlsl");
			ShaderAsset* update = Import("AgeUpdate.hlsl");
			ShaderAsset* output = Import("QuadOutput.hlsl");
			if (!initialize || !update || !output) {
				Delete(initialize);
				Delete(update);
				Delete(output);
				return nullptr;
			}
			EffectDescription description;
			description.timing = _timing;
			description.instancing = _instancing;
			description.timing.cullSimulation = mode != EffectBoundsMode::Automatic;
			description.timing.cullBoundsUpdate = mode != EffectBoundsMode::Automatic;
			EffectProperty lifetime;
			lifetime.name = "Lifetime";
			lifetime.reference = "lifetime";
			lifetime.offset = 96;
			lifetime.defaultValue[0] = 100.0f;
			description.properties.push_back(lifetime);
			EffectProperty center;
			center.name = "Center";
			center.reference = "center";
			center.offset = 100;
			center.components = 3;
			description.properties.push_back(center);
			EffectProperty padding;
			padding.name = "Padding";
			padding.reference = "padding";
			padding.offset = 112;
			padding.components = 3;
			description.properties.push_back(padding);
			description.batchesOffset = 128;
			description.events = {"OnPlay", "OnStop"};
			EffectSpawner spawner;
			spawner.start.push_back("OnPlay");
			spawner.stop.push_back("OnStop");
			EffectSpawnBlock block;
			block.kind = EffectSpawnKind::ConstantRate;
			block.program = AssembleOrFail(Rate(10.0f));
			spawner.blocks.push_back(std::move(block));
			description.spawners.push_back(std::move(spawner));
			EffectSystem system;
			system.capacity = 64;
			system.attributeCount = 2;
			system.stored.push_back({"age", 0, 1});
			system.stored.push_back({"lifetime", 1, 1});
			system.initialize = 0;
			system.update = 1;
			system.outputs.push_back({2, 6});
			system.spawners.push_back(0);
			system.bounds = AssembleOrFail("input 3 center\nconst 3 0x40000000 0x40000000 0x40000000\ninput 3 padding\nroot 0\nroot 1\nroot 2\n");
			system.boundsMode = mode;
			system.measuresBounds = mode == EffectBoundsMode::Automatic;
			description.systems.push_back(std::move(system));
			VisualEffectAsset* asset = New<VisualEffectAsset>(MemTag::Asset);
			Vector<ShaderAsset*> shaders{{initialize, update, output}, GetMemoryResource(MemTag::Asset)};
			if (!EffectAssetAccess::Adopt(*asset, std::move(description), shaders)) {
				Delete(asset);
				return nullptr;
			}
			_assets.push_back(asset);
			return asset;
		}

		static AABB CulledBy(Handle<VisualEffect> effect) {
			const EffectInstance* instance = EffectComponentAccess::Instance(*effect.Get());
			return instance ? instance->GetWorldBounds(effect->GetOwner()->GetWorldMatrix()) : AABB::Empty();
		}

		static EffectFrameCounts Seen(ViewTargetId view, f32 deltaTime) {
			AdvanceEffects(Exactly(deltaTime));
			RenderSystem::SubmitView(view, QuadView());
			RenderSystem::RenderAll();
			return GetEffectFrameCounts();
		}

		static str Rate(f32 perSecond) { return "const 1 " + Bits(perSecond) + "\nroot 0\n"; }
		static str Burst(f32 count) { return "const 1 " + Bits(count) + "\nconst 1 0x00000000\nroot 0\nroot 1\n"; }

		Handle<VisualEffect> AddEffect(VisualEffectAsset* asset) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Effect", _scene);
			Handle<VisualEffect> effect = object->AddComponent<VisualEffect>();
			effect->asset = Handle<VisualEffectAsset>(asset);
			SceneSystem::EndFrame();
			return effect;
		}

		static EffectFrameTime Exactly(f32 deltaTime) {
			EffectFrameTime time;
			time.scaled = {deltaTime, deltaTime, 1};
			time.unscaled = time.scaled;
			time.step = deltaTime;
			return time;
		}

		static void Frame(f32 deltaTime) {
			AdvanceEffects(Exactly(deltaTime));
			RenderSystem::RenderAll();
		}

		static u32 Alive(const ParticleSystem& system) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			u32 counts[4] = {};
			device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(counts), sizeof(counts)}, system.GetListsOffset());
			return counts[system.GetParity()];
		}

		static u32 Alive(Handle<VisualEffect> effect) {
			EffectInstance* instance = EffectComponentAccess::Instance(*effect.Get());
			return instance && instance->GetSystemCount() > 0 ? Alive(instance->GetSystem(0)) : 0u;
		}

		static Vector<f32> Attribute(Handle<VisualEffect> effect, u32 slot, u32 held = 0) {
			const ParticleSystem& system = EffectComponentAccess::Instance(*effect.Get())->GetSystem(held);
			const u32 alive = Alive(system);
			Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
			indices.assign(alive, 0);
			RHI::Device& device = *RenderSystem::GetDevice();
			if (alive > 0) {
				const u64 offset = system.GetListsOffset() + (4ull + static_cast<u64>(system.GetParity()) * system.GetCapacity()) * 4;
				device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(indices.data()), indices.size() * 4}, offset);
			}
			Vector<f32> values{GetMemoryResource(MemTag::Graphics)};
			for (u32 index : indices) {
				f32 value = 0.0f;
				device.ReadbackBuffer(system.GetAttributesBuffer(), {reinterpret_cast<u8*>(&value), 4},
									  system.GetAttributesOffset() + (static_cast<u64>(slot) * system.GetCapacity() + index) * 4);
				values.push_back(value);
			}
			return values;
		}
	};

} // anonymous namespace

TEST_F(VisualEffectComponentFixture, AnOverrideChangesTheReadback) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(8.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> plain = AddEffect(asset);
	Handle<VisualEffect> brief = AddEffect(asset);
	ASSERT_TRUE(brief->SetFloat("Lifetime", 1.5f));
	EXPECT_EQ(brief->properties.size(), 1u);
	EXPECT_EQ(plain->properties.size(), 0u);

	Frame(1.0f);
	EXPECT_EQ(Alive(plain), 8u);
	EXPECT_EQ(Alive(brief), 8u);
	for (f32 lifetime : Attribute(brief, 1)) {
		EXPECT_EQ(lifetime, 1.5f);
	}
	Frame(1.0f);
	Frame(1.0f);
	EXPECT_EQ(Alive(plain), 8u);
	EXPECT_EQ(Alive(brief), 0u);
}

TEST_F(VisualEffectComponentFixture, ScriptReadsCpuSystemsParticlesByName) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeReadAsset(4.0f);
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	EXPECT_EQ(effect->GetParticleCount("Sparks"), 0u);
	Frame(0.25f);
	ASSERT_EQ(effect->GetParticleCount("Sparks"), 4u);
	Frame(0.25f);

	Vec3 positions[8];
	f32 ids[8] = {};
	ASSERT_EQ(effect->GetParticles("Sparks", "position", positions), 4u);
	ASSERT_EQ(effect->GetParticles("Sparks", "particleId", ids), 4u);
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_EQ(positions[i], Vec3(ids[i], 0.25f, 0.0f)) << "particle " << i;
	}
	Vector<f32> sorted{ids, ids + 4, GetMemoryResource(MemTag::Graphics)};
	std::sort(sorted.begin(), sorted.end());
	EXPECT_EQ(sorted, (Vector<f32>{{0.0f, 1.0f, 2.0f, 3.0f}, GetMemoryResource(MemTag::Graphics)}));

	Vec3 two[2];
	EXPECT_EQ(effect->GetParticles("Sparks", "position", two), 2u);
	EXPECT_EQ(two[1], positions[1]);
	EXPECT_EQ(effect->GetParticles("Sparks", "position", two, 3), 1u);
	EXPECT_EQ(two[0], positions[3]);
	EXPECT_EQ(effect->GetParticles("Sparks", "position", two, 4), 0u);

	f32 floats[8] = {};
	EXPECT_EQ(effect->GetParticles("Sparks", "position", floats), 0u);
	EXPECT_EQ(effect->GetParticles("Sparks", "color", positions), 0u);
	EXPECT_EQ(effect->GetParticleCount("Fire"), 0u);
	EffectInstance* instance = EffectComponentAccess::Instance(*effect.Get());
	ASSERT_NE(instance, nullptr);
	ASSERT_EQ(Alive(instance->GetSystem(1)), 4u);
	EXPECT_EQ(effect->GetParticleCount("Smoke"), 0u);
	EXPECT_EQ(effect->GetParticles("Smoke", "age", floats), 0u);

	effect->SetEnabled(false);
	EXPECT_EQ(effect->GetParticleCount("Sparks"), 0u);
}

TEST_F(VisualEffectComponentFixture, UpdatesRunInTurn) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeReadAsset(4.0f);
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	Frame(0.25f);
	EffectInstance* instance = EffectComponentAccess::Instance(*effect.Get());
	ASSERT_NE(instance, nullptr);
	ASSERT_EQ(instance->GetSystemCount(), 2u);
	const f32 updates[] = {0.25f, 0.5f};
	instance->Advance(updates, Mat4::Identity(), {});
	Vector<EffectSent> sent{GetMemoryResource(MemTag::Graphics)};
	Vector<f32> values{GetMemoryResource(MemTag::Graphics)};
	instance->TakeSent(sent, values);
	RenderSystem::RenderAll();

	EXPECT_EQ(instance->GetTime(), 1.0f);
	EXPECT_EQ(instance->GetCpuSystem(0)->GetFrame(), 3u);
	EXPECT_EQ(instance->GetSystem(1).GetSimulatedFrames(), 3u);
	Vec3 positions[4];
	ASSERT_EQ(effect->GetParticles("Sparks", "position", positions), 4u);
	for (const Vec3& position : positions) {
		EXPECT_EQ(position.y, 0.75f);
	}
	const Vector<f32> ages = Attribute(effect, 0, 1);
	ASSERT_EQ(ages.size(), 4u);
	for (f32 age : ages) {
		EXPECT_EQ(age, 0.75f);
	}
	ASSERT_EQ(sent.size(), 8u);
	for (u32 i = 0; i < sent.size(); ++i) {
		EXPECT_EQ(values[sent[i].event.values + 1], i < 4 ? 0.25f : 0.75f);
	}
}

TEST_F(VisualEffectComponentFixture, PrewarmRunsOnStartAndReinit) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	_timing.prewarmStepCount = 10;
	_timing.prewarmDeltaTime = 0.125f;
	VisualEffectAsset* asset = MakeReadAsset(4.0f);
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	const auto prewarmed = [&](const char* when) {
		Frame(0.25f);
		EffectInstance* instance = EffectComponentAccess::Instance(*effect.Get());
		ASSERT_NE(instance, nullptr);
		EXPECT_EQ(instance->GetTime(), 1.5f) << when;
		EXPECT_EQ(instance->GetCpuSystem(0)->GetFrame(), 11u) << when;
		EXPECT_EQ(instance->GetSystem(1).GetSimulatedFrames(), 11u) << when;
		Vec3 positions[4];
		ASSERT_EQ(effect->GetParticles("Sparks", "position", positions), 4u) << when;
		for (const Vec3& position : positions) {
			EXPECT_EQ(position.y, 1.375f) << when;
		}
		for (f32 age : Attribute(effect, 0, 1)) {
			EXPECT_EQ(age, 1.375f) << when;
		}
		Vector<EffectSent> sent{GetMemoryResource(MemTag::Graphics)};
		Vector<f32> values{GetMemoryResource(MemTag::Graphics)};
		instance->TakeSent(sent, values);
		EXPECT_EQ(sent.size(), 4u) << when;
	};
	prewarmed("built");
	Frame(0.25f);
	EXPECT_EQ(EffectComponentAccess::Instance(*effect.Get())->GetSystem(1).GetSimulatedFrames(), 12u);
	effect->Reinit();
	prewarmed("reinitialized");
}

TEST_F(VisualEffectComponentFixture, EffectAdvancesByTiming) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const VisualEffectsSettings& settings = GetEffectSettings();
	ASSERT_EQ(settings.fixedTimeStep, 1.0f / 60.0f);
	ASSERT_EQ(settings.maxDeltaTime, 0.05f);
	EffectTiming timings[4];
	timings[1].exactFixedTime = true;
	timings[2].fixedDeltaTime = false;
	timings[3].exactFixedTime = true;
	timings[3].ignoreTimeScale = true;
	Handle<VisualEffect> effects[4];
	for (u32 i = 0; i < 4; ++i) {
		_timing = timings[i];
		VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::ConstantRate, Rate(0.0f));
		if (!asset) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		effects[i] = AddEffect(asset);
	}
	const auto frame = [](f32 deltaTime, f32 unscaledDeltaTime) {
		AdvanceEffects(TickEffectClock(deltaTime, unscaledDeltaTime));
		RenderSystem::RenderAll();
	};
	const auto updates = [&](u32 i) { return EffectComponentAccess::Instance(*effects[i].Get())->GetSystem(0).GetSimulatedFrames(); };
	const auto time = [&](u32 i) { return EffectComponentAccess::Instance(*effects[i].Get())->GetTime(); };

	frame(1.0f / 30.0f, 1.0f / 30.0f);
	const u32 first[4] = {1, 2, 1, 2};
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_EQ(updates(i), first[i]) << "effect " << i;
		EXPECT_FLOAT_EQ(time(i), 1.0f / 30.0f) << "effect " << i;
	}
	frame(0.0f, 0.2f);
	const u32 second[4] = {2, 3, 2, 5};
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_EQ(updates(i), second[i]) << "effect " << i;
	}
	EXPECT_FLOAT_EQ(time(0), 1.0f / 30.0f);
	EXPECT_FLOAT_EQ(time(2), 1.0f / 30.0f);
	EXPECT_FLOAT_EQ(time(3), 1.0f / 30.0f + 0.05f);
}

TEST_F(VisualEffectComponentFixture, FrameDispatchesNeededOnly) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	for (u32 i = 0; i < 3; ++i) {
		_timing.exactFixedTime = i == 2;
		VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::ConstantRate, Rate(60.0f));
		if (!asset) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		AddEffect(asset);
	}
	const auto frame = []() {
		AdvanceEffects(TickEffectClock(1.0f / 30.0f, 1.0f / 30.0f));
		RenderSystem::RenderAll();
		return GetEffectFrameCounts();
	};
	EffectFrameCounts counts = frame();
	EXPECT_EQ(counts.dispatches, 15u);
	counts = frame();
	EXPECT_EQ(counts.effects, 3u);
	EXPECT_EQ(counts.updates, 4u);
	EXPECT_EQ(counts.systemUpdates, 4u);
	EXPECT_EQ(counts.dispatches, 12u);
	EXPECT_EQ(counts.copies, 0u);
}

TEST_F(VisualEffectComponentFixture, PausedEffectRecordsNothing) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeReadAsset(4.0f);
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> playing = AddEffect(asset);
	Handle<VisualEffect> paused = AddEffect(asset);
	const auto frame = [](f32 deltaTime) {
		AdvanceEffects(TickEffectClock(deltaTime, deltaTime));
		RenderSystem::RenderAll();
		return GetEffectFrameCounts();
	};
	frame(1.0f / 60.0f);
	EffectComponentAccess::Instance(*paused.Get())->SetPaused(true);
	EffectFrameCounts counts = frame(1.0f / 60.0f);
	EXPECT_EQ(counts.effects, 1u);
	EXPECT_EQ(counts.updates, 1u);
	EXPECT_EQ(counts.systemUpdates, 2u);
	EXPECT_EQ(counts.copies, 3u);
	EXPECT_EQ(counts.dispatches, 3u);
	EXPECT_EQ(counts.alive, 8u);
	counts = frame(1.0f / 120.0f);
	EXPECT_EQ(counts.updates, 1u);
	EXPECT_EQ(EffectComponentAccess::Instance(*playing.Get())->GetTime(), 2.0f / 60.0f);
	EXPECT_EQ(counts.copies, 3u);
}

TEST_F(VisualEffectComponentFixture, CulledEffectWaitsUntilSeen) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeSeenAsset(Rate(60.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const ViewTargetId view = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(view, INVALID_VIEW_TARGET);
	Handle<VisualEffect> effect = AddEffect(asset);
	effect->GetOwner()->SetLocalPosition(Vec3(50.0f, 0.0f, 0.0f));
	constexpr f32 STEP = 1.0f / 60.0f;

	EffectFrameCounts counts = Seen(view, STEP);
	EXPECT_EQ(counts.effects, 1u);
	ASSERT_TRUE(effect->IsCulled());
	counts = Seen(view, STEP);
	EXPECT_EQ(counts.effects, 0u);
	EXPECT_EQ(counts.culled, 1u);
	EXPECT_EQ(counts.updates, 0u);
	EXPECT_EQ(counts.dispatches, 0u);
	EXPECT_EQ(EffectComponentAccess::Instance(*effect.Get())->GetTime(), STEP);

	effect->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, 0.0f));
	counts = Seen(view, STEP);
	EXPECT_EQ(counts.culled, 1u);
	EXPECT_FALSE(effect->IsCulled());
	counts = Seen(view, STEP);
	EXPECT_EQ(counts.effects, 1u);
	EXPECT_EQ(counts.culled, 0u);
	EXPECT_EQ(counts.updates, 1u);
	EXPECT_EQ(counts.dispatches, 3u);
	EXPECT_EQ(EffectComponentAccess::Instance(*effect.Get())->GetTime(), 2.0f * STEP);
	EXPECT_EQ(Alive(effect), 2u);
	RenderSystem::DestroyViewTarget(view);
}

TEST_F(VisualEffectComponentFixture, RebuiltEffectAdvancesFirstFrameWhateverVerdict) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeSeenAsset(Rate(60.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const ViewTargetId view = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(view, INVALID_VIEW_TARGET);
	Handle<VisualEffect> effect = AddEffect(asset);
	effect->GetOwner()->SetLocalPosition(Vec3(50.0f, 0.0f, 0.0f));
	constexpr f32 STEP = 1.0f / 60.0f;
	Seen(view, STEP);
	ASSERT_TRUE(effect->IsCulled());
	ASSERT_EQ(Seen(view, STEP).culled, 1u);

	ASSERT_TRUE(effect->SetVector3("Center", Vec3(-50.0f, 0.0f, 0.0f)));
	effect->Reinit();
	EXPECT_EQ(Seen(view, STEP).effects, 1u);
	EXPECT_FALSE(effect->IsCulled());
	EXPECT_EQ(Seen(view, STEP).effects, 1u);
	RenderSystem::DestroyViewTarget(view);
}

TEST_F(VisualEffectComponentFixture, CullingChoicesHold) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	Handle<VisualEffect> effects[3];
	const bool cullsSimulation[3] = {true, true, false};
	const bool cullsBounds[3] = {true, false, false};
	for (u32 i = 0; i < 3; ++i) {
		_timing.cullSimulation = cullsSimulation[i];
		_timing.cullBoundsUpdate = cullsBounds[i];
		VisualEffectAsset* asset = MakeSeenAsset(Rate(60.0f));
		if (!asset) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		effects[i] = AddEffect(asset);
		ASSERT_TRUE(effects[i]->SetVector3("Center", Vec3(50.0f, 0.0f, 0.0f)));
	}
	const ViewTargetId view = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(view, INVALID_VIEW_TARGET);
	constexpr f32 STEP = 1.0f / 60.0f;

	EffectFrameCounts counts = Seen(view, STEP);
	EXPECT_EQ(counts.effects, 3u);
	EXPECT_EQ(RenderSystem::GetStats().proceduralCulled, 3u);
	counts = Seen(view, STEP);
	EXPECT_EQ(counts.effects, 1u);
	EXPECT_EQ(counts.culled, 2u);
	EXPECT_EQ(counts.dispatches, 3u);
	EXPECT_EQ(counts.draws, 0u);

	for (Handle<VisualEffect>& effect : effects) {
		ASSERT_TRUE(effect->SetVector3("Center", Vec3(0.0f, 0.0f, 0.0f)));
	}
	counts = Seen(view, STEP);
	EXPECT_EQ(counts.culled, 2u);
	EXPECT_TRUE(effects[0]->IsCulled());
	EXPECT_FALSE(effects[1]->IsCulled());
	EXPECT_FALSE(effects[2]->IsCulled());
	counts = Seen(view, STEP);
	EXPECT_EQ(counts.effects, 2u);
	EXPECT_EQ(counts.culled, 1u);
	EXPECT_TRUE(effects[0]->IsCulled());
	RenderSystem::DestroyViewTarget(view);
}

TEST_F(VisualEffectComponentFixture, CulledEffectEventsToggleSpawners) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeSeenAsset(Rate(60.0f), {"Hit"});
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const ViewTargetId view = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(view, INVALID_VIEW_TARGET);
	Handle<VisualEffect> effect = AddEffect(asset);
	effect->GetOwner()->SetLocalPosition(Vec3(50.0f, 0.0f, 0.0f));
	constexpr f32 STEP = 1.0f / 60.0f;

	Seen(view, STEP);
	ASSERT_TRUE(effect->IsCulled());
	for (u32 i = 0; i < 3; ++i) {
		effect->SendEvent("Hit");
	}
	effect->Stop();
	EXPECT_EQ(Seen(view, STEP).culled, 1u);
	effect->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, 0.0f));
	Seen(view, STEP);
	ASSERT_FALSE(effect->IsCulled());
	EXPECT_EQ(Seen(view, STEP).effects, 1u);
	EXPECT_EQ(Alive(effect), 1u);

	effect->Play();
	Seen(view, STEP);
	EXPECT_EQ(Alive(effect), 2u);
	RenderSystem::DestroyViewTarget(view);
}

TEST_F(VisualEffectComponentFixture, BoundsModesSetCullBox) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* manual = MakeBoundedAsset(EffectBoundsMode::Manual);
	VisualEffectAsset* recorded = MakeBoundedAsset(EffectBoundsMode::Recorded);
	VisualEffectAsset* automatic = MakeBoundedAsset(EffectBoundsMode::Automatic);
	if (!manual || !recorded || !automatic) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const auto expectBox = [](const AABB& box, const Vec3& low, const Vec3& high, const char* what) {
		SCOPED_TRACE(what);
		for (u32 axis = 0; axis < 3; ++axis) {
			EXPECT_FLOAT_EQ(box.min[axis], low[axis]) << axis;
			EXPECT_FLOAT_EQ(box.max[axis], high[axis]) << axis;
		}
	};
	const auto placed = [this](VisualEffectAsset* asset, const Vec3& padding) {
		Handle<VisualEffect> effect = AddEffect(asset);
		effect->GetOwner()->SetLocalPosition(Vec3(3.0f, 0.0f, 0.0f));
		effect->SetVector3("Padding", padding);
		return effect;
	};
	Handle<VisualEffect> unpadded = placed(manual, Vec3(5.0f, 5.0f, 5.0f));
	Handle<VisualEffect> padded = placed(recorded, Vec3(0.5f, 0.0f, -0.25f));
	Handle<VisualEffect> squashed = placed(recorded, Vec3(0.0f, -3.0f, 0.0f));
	Handle<VisualEffect> measured = placed(automatic, Vec3(0.5f, 0.5f, 0.5f));
	Frame(0.1f);
	expectBox(CulledBy(unpadded), Vec3(2.0f, -1.0f, -1.0f), Vec3(4.0f, 1.0f, 1.0f), "Manual, its padding unread");
	expectBox(CulledBy(padded), Vec3(1.5f, -1.0f, -0.75f), Vec3(4.5f, 1.0f, 0.75f), "Recorded, padded");
	expectBox(CulledBy(squashed), Vec3(2.0f, 0.0f, -1.0f), Vec3(4.0f, 0.0f, 1.0f), "Recorded, an axis squashed to its middle");
	expectBox(CulledBy(measured), Vec3(2.5f, -0.5f, -0.5f), Vec3(3.5f, 0.5f, 0.5f), "Automatic, before a measure");
	for (u32 i = 0; i <= RHI::FRAMES_IN_FLIGHT; ++i) {
		Frame(0.1f);
	}
	const EffectInstance* instance = EffectComponentAccess::Instance(*measured.Get());
	ASSERT_TRUE(instance->GetMeasuredBounds(0).known);
	EXPECT_FALSE(instance->GetMeasuredBounds(0).box.IsValid());
	expectBox(CulledBy(measured), Vec3(3.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f), "Automatic, measuring nothing");
	EXPECT_FALSE(EffectComponentAccess::Instance(*padded.Get())->GetMeasuredBounds(0).known);
}

namespace {

	const ParticleBatch* BatchOf(Handle<VisualEffect> effect) {
		const EffectInstance* instance = EffectComponentAccess::Instance(*effect.Get());
		return instance && instance->GetSystemCount() > 0 ? instance->GetSystem(0).GetBatch() : nullptr;
	}

} // anonymous namespace

TEST_F(VisualEffectComponentFixture, SameAssetSharesBatch) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::ConstantRate, Rate(60.0f));
	_instancing.mode = EFFECT_INSTANCING_DISABLED;
	VisualEffectAsset* alone = MakeAsset(EffectSpawnKind::ConstantRate, Rate(60.0f));
	if (!asset || !alone) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> batched[8];
	for (Handle<VisualEffect>& effect : batched) {
		effect = AddEffect(asset);
	}
	Frame(1.0f / 60.0f);
	EXPECT_EQ(GetEffectFrameCounts().dispatches, 4u);
	for (const Handle<VisualEffect>& effect : batched) {
		ASSERT_NE(BatchOf(effect), nullptr);
		EXPECT_EQ(BatchOf(effect), BatchOf(batched[0]));
		EXPECT_EQ(EffectComponentAccess::Instance(*effect.Get())->GetBatchSlots(), 64u);
	}
	Frame(1.0f / 60.0f);
	EXPECT_EQ(GetEffectFrameCounts().dispatches, 3u);
	Frame(1.0f / 60.0f);
	for (u32 i = 0; i < 4; ++i) {
		batched[i]->Stop();
	}
	for (int frame = 0; frame < 3; ++frame) {
		Frame(1.0f / 60.0f);
	}
	for (u32 i = 0; i < 8; ++i) {
		EXPECT_EQ(Alive(batched[i]), i < 4 ? 3u : 6u) << "effect " << i;
	}

	for (Handle<VisualEffect>& effect : batched) {
		SceneSystem::Destroy(effect->GetOwner());
	}
	SceneSystem::EndFrame();
	Handle<VisualEffect> apart[8];
	for (Handle<VisualEffect>& effect : apart) {
		effect = AddEffect(alone);
	}
	Frame(1.0f / 60.0f);
	Frame(1.0f / 60.0f);
	EXPECT_EQ(GetEffectFrameCounts().dispatches, 24u);
	EXPECT_NE(BatchOf(apart[0]), BatchOf(apart[1]));
	EXPECT_EQ(EffectComponentAccess::Instance(*apart[0].Get())->GetBatchSlots(), 1u);
}

TEST_F(VisualEffectComponentFixture, InstancingOffGetsOwnBatch) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::ConstantRate, Rate(60.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> first = AddEffect(asset);
	Handle<VisualEffect> second = AddEffect(asset);
	Handle<VisualEffect> apart = AddEffect(asset);
	apart->allowInstancing = false;
	Frame(1.0f / 60.0f);
	Frame(1.0f / 60.0f);
	EXPECT_EQ(BatchOf(first), BatchOf(second));
	EXPECT_NE(BatchOf(apart), BatchOf(first));
	EXPECT_EQ(EffectComponentAccess::Instance(*apart.Get())->GetBatchSlots(), 1u);
	EXPECT_EQ(Alive(apart), 2u);

	apart->allowInstancing = true;
	Frame(1.0f / 60.0f);
	EXPECT_EQ(BatchOf(apart), BatchOf(first));
	EXPECT_EQ(EffectComponentAccess::Instance(*apart.Get())->GetTime(), 1.0f / 60.0f);
	EXPECT_EQ(Alive(apart), 1u);
	apart->allowInstancing = false;
	Frame(1.0f / 60.0f);
	EXPECT_NE(BatchOf(apart), BatchOf(first));
	EXPECT_EQ(Alive(first), 4u);
}

TEST_F(VisualEffectComponentFixture, CapacitySetsBatchSize) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	_instancing = {EFFECT_INSTANCING_CUSTOM, 2};
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::ConstantRate, Rate(60.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effects[3] = {AddEffect(asset), AddEffect(asset), AddEffect(asset)};
	Frame(1.0f / 60.0f);
	EXPECT_EQ(BatchOf(effects[0]), BatchOf(effects[1]));
	EXPECT_NE(BatchOf(effects[2]), BatchOf(effects[0]));
	EXPECT_EQ(EffectComponentAccess::Instance(*effects[2].Get())->GetBatchSlots(), 2u);
	EXPECT_EQ(GetEffectFrameCounts().batches, 2u);

	constexpr u64 MIB = 1ull << 20;
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_CUSTOM, 5}, 4 * MIB), 5u);
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_CUSTOM, 0}, 64), 1u);
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_CUSTOM, 70000}, 64), 65535u);
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_DISABLED, 64}, 64), 1u);
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_AUTOMATIC, 64}, 1024), 64u);
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_AUTOMATIC, 64}, MIB / 3), 3u);
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_AUTOMATIC, 64}, 2 * MIB), 1u);
	EXPECT_EQ(EffectBatchSlots({EFFECT_INSTANCING_AUTOMATIC, 64}, 0), 64u);
}

TEST_F(VisualEffectComponentFixture, EffectLeavingBatchLeavesOthersPlaying) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::ConstantRate, Rate(60.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> first = AddEffect(asset);
	Handle<VisualEffect> middle = AddEffect(asset);
	Handle<VisualEffect> last = AddEffect(asset);
	for (int frame = 0; frame < 4; ++frame) {
		Frame(1.0f / 60.0f);
	}
	const u32 slot = EffectComponentAccess::Instance(*middle.Get())->GetSystem(0).GetSlot();
	ASSERT_EQ(Alive(middle), 4u);
	SceneSystem::Destroy(middle->GetOwner());
	SceneSystem::EndFrame();
	Frame(1.0f / 60.0f);
	EXPECT_EQ(Alive(first), 5u);
	EXPECT_EQ(Alive(last), 5u);
	Handle<VisualEffect> next = AddEffect(asset);
	Frame(1.0f / 60.0f);
	EXPECT_EQ(BatchOf(next), BatchOf(first));
	EXPECT_EQ(EffectComponentAccess::Instance(*next.Get())->GetSystem(0).GetSlot(), slot);
	EXPECT_EQ(Alive(next), 1u);
	EXPECT_EQ(Alive(first), 6u);
}

TEST_F(VisualEffectComponentFixture, ScriptsHearWhatCpuSystemsParticlesSend) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeReadAsset(4.0f);
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	EffectEventRecorder::GetReflectType();
	Handle<VisualEffect> effect = AddEffect(asset);
	SceneObject* owner = effect->GetOwner().Get();
	ASSERT_NE(owner, nullptr);
	Handle<EffectEventRecorder> first = owner->AddComponent<EffectEventRecorder>();
	Handle<EffectEventRecorder> reinits = owner->AddComponent<EffectEventRecorder>();
	Handle<EffectEventRecorder> off = owner->AddComponent<EffectEventRecorder>();
	Handle<EffectEventRecorder> elsewhere = SceneSystem::CreateObject("Elsewhere", _scene)->AddComponent<EffectEventRecorder>();
	ASSERT_TRUE(first.IsValid() && reinits.IsValid() && off.IsValid() && elsewhere.IsValid());
	reinits->reinitOnFirst = true;
	off->SetEnabled(false);
	SceneSystem::EndFrame();
	const auto delivered = [](f32 deltaTime) {
		AdvanceEffects(Exactly(deltaTime));
		DeliverEffectEvents();
		RenderSystem::RenderAll();
	};

	delivered(0.25f);
	EXPECT_TRUE(first->heard.empty());
	Frame(0.25f);
	delivered(0.25f);
	ASSERT_EQ(first->heard.size(), 4u);
	EXPECT_EQ(reinits->heard.size(), 4u);
	EXPECT_TRUE(off->heard.empty());
	EXPECT_TRUE(elsewhere->heard.empty());
	for (u32 i = 0; i < first->heard.size(); ++i) {
		const EffectEventRecorder::Heard& heard = first->heard[i];
		EXPECT_EQ(heard.name, "Rose");
		EXPECT_EQ(heard.id.x, static_cast<f32>(i));
		EXPECT_EQ(heard.position.x, heard.id.x) << "event " << i;
		EXPECT_EQ(heard.position.y, 0.5f) << "event " << i;
		EXPECT_EQ(heard.count.x, 1.0f) << "event " << i;
		EXPECT_EQ(reinits->heard[i].id.x, heard.id.x);
	}
	EXPECT_EQ(effect->GetParticleCount("Sparks"), 0u);
}

TEST_F(VisualEffectComponentFixture, ScriptHearsBurstEvents) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(3.0f), "AgeInitialize.hlsl", 2u, false, {"OnPlay"}, {}, [](EffectDescription& description) {
		EffectSpawnBlock color;
		color.kind = EffectSpawnKind::SetAttribute;
		color.program = AssembleOrFail("const 3 " + Bits(0.25f) + " " + Bits(0.5f) + " " + Bits(0.75f) + "\nroot 0\n");
		color.attribute = "color";
		color.components = 3;
		description.spawners[0].blocks.push_back(std::move(color));
		description.spawners[0].outputEvents.push_back("Burst");
		description.relays.push_back({"Hit", "Echo"});
		description.events.push_back("Hit");
	});
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	EffectEventRecorder::GetReflectType();
	Handle<VisualEffect> effect = AddEffect(asset);
	Handle<EffectEventRecorder> recorder = effect->GetOwner()->AddComponent<EffectEventRecorder>();
	ASSERT_TRUE(recorder.IsValid());
	SceneSystem::EndFrame();
	const auto delivered = [](f32 deltaTime) {
		AdvanceEffects(Exactly(deltaTime));
		DeliverEffectEvents();
		RenderSystem::RenderAll();
	};

	delivered(0.25f);
	ASSERT_EQ(recorder->heard.size(), 1u);
	const EffectEventRecorder::Heard first = recorder->heard[0];
	EXPECT_EQ(first.name, "Burst");
	EXPECT_EQ(first.count.x, 3.0f);
	EXPECT_EQ(first.color.x, 0.25f);
	EXPECT_EQ(first.color.z, 0.75f);
	EXPECT_EQ(first.position.y, 0.0f);
	delivered(0.25f);
	EXPECT_EQ(recorder->heard.size(), 1u);

	VisualEffectEventAttributes at;
	at.SetVector3("position", Vec3(1.0f, 2.0f, 3.0f));
	at.SetVector3("color", Vec3(9.0f, 9.0f, 9.0f));
	effect->Reinit();
	effect->SendEvent("OnPlay", at);
	delivered(0.25f);
	ASSERT_EQ(recorder->heard.size(), 2u);
	EXPECT_EQ(recorder->heard[1].name, "Burst");
	EXPECT_EQ(recorder->heard[1].position.y, 2.0f);
	EXPECT_EQ(recorder->heard[1].color.y, 0.5f);

	VisualEffectEventAttributes hit;
	hit.SetVector3("position", Vec3(4.0f, 5.0f, 6.0f));
	hit.SetFloat("spawnCount", 7.0f);
	effect->SendEvent("Hit", hit);
	effect->SendEvent("Hit");
	delivered(0.25f);
	ASSERT_EQ(recorder->heard.size(), 4u);
	EXPECT_EQ(recorder->heard[2].name, "Echo");
	EXPECT_EQ(recorder->heard[2].position.z, 6.0f);
	EXPECT_EQ(recorder->heard[2].count.x, 7.0f);
	EXPECT_EQ(recorder->heard[3].name, "Echo");
	EXPECT_EQ(recorder->heard[3].position.z, 0.0f);
	delivered(0.25f);
	EXPECT_EQ(recorder->heard.size(), 4u);
}

TEST_F(VisualEffectComponentFixture, PlayStopReachSpawners) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* rated = MakeAsset(EffectSpawnKind::ConstantRate, Rate(4.0f));
	VisualEffectAsset* custom = MakeAsset(EffectSpawnKind::ConstantRate, Rate(4.0f), false, {"Burst"});
	if (!rated || !custom) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(rated);
	Handle<VisualEffect> waiting = AddEffect(custom);
	Frame(1.0f);
	EXPECT_EQ(Alive(effect), 4u);
	EXPECT_EQ(Alive(waiting), 0u);

	effect->Stop();
	Frame(1.0f);
	EXPECT_EQ(Alive(effect), 4u);
	effect->Play();
	waiting->SendEvent("Burst");
	Frame(1.0f);
	EXPECT_EQ(Alive(effect), 8u);
	EXPECT_EQ(Alive(waiting), 4u);
}

TEST_F(VisualEffectComponentFixture, EventsAttributesReachParticlesItSpawns) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(3.0f), true);
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	Frame(1.0f);
	for (f32 x : Attribute(effect, 2)) {
		EXPECT_EQ(x, 0.0f);
	}

	VisualEffectEventAttributes hit;
	hit.SetVector3("position", Vec3(1.0f, 2.0f, 3.0f));
	effect->Reinit();
	effect->SendEvent("OnPlay", hit);
	Frame(1.0f);
	const Vector<f32> xs = Attribute(effect, 2);
	const Vector<f32> zs = Attribute(effect, 4);
	ASSERT_EQ(xs.size(), 3u);
	for (size_t i = 0; i < xs.size(); ++i) {
		EXPECT_EQ(xs[i], 1.0f);
		EXPECT_EQ(zs[i], 3.0f);
	}
}

TEST_F(VisualEffectComponentFixture, InitializeEventSpawnsPerSend) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, "", "SourceInitialize.hlsl", 5u, true, {}, {"Hit"});
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	Frame(1.0f);
	EXPECT_EQ(Alive(effect), 0u);

	VisualEffectEventAttributes three;
	three.SetFloat("spawnCount", 3.7f);
	three.SetVector3("position", Vec3(1.0f, 2.0f, 3.0f));
	VisualEffectEventAttributes one;
	one.SetVector3("position", Vec3(4.0f, 5.0f, 6.0f));
	effect->SendEvent("Hit", three);
	effect->SendEvent("Hit", one);
	Frame(1.0f);
	const Vector<f32> xs = Attribute(effect, 2);
	ASSERT_EQ(xs.size(), 4u);
	EXPECT_EQ(std::count(xs.begin(), xs.end(), 1.0f), 3);
	EXPECT_EQ(std::count(xs.begin(), xs.end(), 4.0f), 1);

	VisualEffectEventAttributes half;
	half.SetFloat("spawnCount", 0.5f);
	effect->SendEvent("Hit", half);
	effect->Play();
	effect->Stop();
	effect->SendEvent("Hit");
	Frame(1.0f);
	const Vector<f32> after = Attribute(effect, 2);
	EXPECT_EQ(after.size(), 5u);
	EXPECT_EQ(std::count(after.begin(), after.end(), 0.0f), 1);
}

TEST_F(VisualEffectComponentFixture, ReinitEmptiesSystems) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(8.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	Frame(1.0f);
	Frame(1.0f);
	ASSERT_EQ(Alive(effect), 8u);
	EXPECT_EQ(EffectComponentAccess::Instance(*effect.Get())->GetTime(), 2.0f);

	effect->Reinit();
	Frame(0.5f);
	EXPECT_EQ(Alive(effect), 8u);
	EXPECT_EQ(EffectComponentAccess::Instance(*effect.Get())->GetTime(), 0.5f);
}

TEST_F(VisualEffectComponentFixture, DisableFreesSystems) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	_instancing.mode = EFFECT_INSTANCING_DISABLED;
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(8.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	Frame(1.0f);
	RenderSystem::GetDevice()->WaitIdle();
	const u32 withSystem = RenderSystem::GetStats().liveBuffers;
	EXPECT_EQ(GetSimulation().GetSystemCount(), 1u);

	effect->SetEnabled(false);
	EXPECT_EQ(EffectComponentAccess::Instance(*effect.Get()), nullptr);
	EXPECT_EQ(GetSimulation().GetSystemCount(), 0u);
	for (int i = 0; i < 3; ++i) {
		RenderSystem::RenderAll();
	}
	RenderSystem::GetDevice()->WaitIdle();
	EXPECT_EQ(RenderSystem::GetStats().liveBuffers, withSystem - 3);

	effect->SetEnabled(true);
	Frame(1.0f);
	EXPECT_EQ(Alive(effect), 8u);

	SceneSystem::Destroy(effect->GetOwner());
	SceneSystem::EndFrame();
	Frame(1.0f);
	EXPECT_EQ(GetEffectCount(), 0u);
}

TEST_F(VisualEffectComponentFixture, SettingPropertyAssetHasNotChangesNothing) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(1.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	EXPECT_FALSE(effect->SetFloat("Speed", 2.0f));
	EXPECT_FALSE(effect->SetVector3("Lifetime", Vec3(1.0f, 2.0f, 3.0f)));
	EXPECT_FALSE(effect->SetInt("Lifetime", 3));
	EXPECT_TRUE(effect->properties.empty());
	EXPECT_TRUE(effect->SetFloat("Lifetime", 4.0f));
	EXPECT_TRUE(effect->SetFloat("Lifetime", 5.0f));
	ASSERT_EQ(effect->properties.size(), 1u);
	EXPECT_EQ(effect->properties[0].value.x, 5.0f);
}

TEST_F(VisualEffectComponentFixture, UntickedOverrideUsesAsset) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(4.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	effect->properties.push_back({str("Lifetime"), Vec4(1.5f, 0.0f, 0.0f, 0.0f), false});
	Frame(1.0f);
	ASSERT_EQ(Alive(effect), 4u);
	for (f32 lifetime : Attribute(effect, 1)) {
		EXPECT_EQ(lifetime, 100.0f);
	}

	effect->Reinit();
	EXPECT_TRUE(effect->SetFloat("Lifetime", 2.5f));
	ASSERT_EQ(effect->properties.size(), 1u);
	EXPECT_TRUE(effect->properties[0].overridden);
	Frame(1.0f);
	ASSERT_EQ(Alive(effect), 4u);
	for (f32 lifetime : Attribute(effect, 1)) {
		EXPECT_EQ(lifetime, 2.5f);
	}
}

TEST_F(VisualEffectComponentFixture, RecompileRestartsInstances) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* live = MakeAsset(EffectSpawnKind::SingleBurst, Burst(8.0f));
	VisualEffectAsset* compiled = MakeAsset(EffectSpawnKind::SingleBurst, Burst(3.0f));
	if (!live || !compiled) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> plain = AddEffect(live);
	Handle<VisualEffect> brief = AddEffect(live);
	ASSERT_TRUE(brief->SetFloat("Lifetime", 1.5f));
	Frame(1.0f);
	ASSERT_EQ(Alive(plain), 8u);
	const u32 generation = EffectAssetAccess::Generation(*live);

	EffectAssetAccess::Replace(*live, *compiled);
	EXPECT_GT(EffectAssetAccess::Generation(*live), generation);
	EXPECT_EQ(EffectComponentAccess::Instance(*plain.Get())->GetSystemCount(), 0u);
	EXPECT_EQ(EffectComponentAccess::Instance(*brief.Get())->GetSystemCount(), 0u);
	RenderSystem::RenderAll();

	Frame(1.0f);
	EXPECT_EQ(plain->asset.Get(), live);
	EXPECT_EQ(Alive(plain), 3u);
	ASSERT_EQ(Alive(brief), 3u);
	for (f32 lifetime : Attribute(brief, 1)) {
		EXPECT_EQ(lifetime, 1.5f);
	}
}

TEST_F(VisualEffectComponentFixture, PlayControlsDriveClock) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::ConstantRate, Rate(4.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> effect = AddEffect(asset);
	Frame(1.0f);
	EffectInstance* instance = EffectComponentAccess::Instance(*effect.Get());
	ASSERT_EQ(Alive(effect), 4u);
	const u32 simulated = instance->GetSystem(0).GetSimulatedFrames();

	instance->SetPaused(true);
	effect->Stop();
	Frame(1.0f);
	Frame(1.0f);
	EXPECT_EQ(instance->GetTime(), 1.0f);
	EXPECT_EQ(instance->GetSystem(0).GetSimulatedFrames(), simulated);
	EXPECT_EQ(Alive(effect), 4u);

	instance->Step();
	Frame(0.25f);
	EXPECT_TRUE(instance->IsPaused());
	EXPECT_EQ(instance->GetTime(), 1.25f);
	EXPECT_EQ(instance->GetSystem(0).GetSimulatedFrames(), simulated + 1);
	Frame(1.0f);
	EXPECT_EQ(Alive(effect), 4u);

	instance->SetPaused(false);
	instance->SetControlRate(0.5f);
	effect->Play();
	Frame(1.0f);
	EXPECT_EQ(instance->GetTime(), 1.75f);
	EXPECT_EQ(effect->playRate, 1.0f);
	EXPECT_EQ(Alive(effect), 6u);

	instance->SetPaused(true);
	effect->Reinit();
	Frame(1.0f);
	EXPECT_TRUE(instance->IsPaused());
	EXPECT_EQ(instance->GetTime(), 0.0f);
	EXPECT_EQ(instance->GetControlRate(), 0.5f);
}

TEST_F(VisualEffectComponentFixture, WatchedCountsArriveLate) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(80.0f));
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> watched = AddEffect(asset);
	Handle<VisualEffect> paused = AddEffect(asset);
	EffectComponentAccess::Instance(*watched.Get())->WatchCounts(true);
	Frame(1.0f);
	EffectInstance* instance = EffectComponentAccess::Instance(*watched.Get());
	EffectInstance* late = EffectComponentAccess::Instance(*paused.Get());
	late->SetPaused(true);
	for (u32 frame = 1; frame <= RHI::FRAMES_IN_FLIGHT; ++frame) {
		EXPECT_FALSE(instance->GetSystem(0).GetCounts().known);
		Frame(1.0f);
	}
	const ParticleCounts& counts = instance->GetSystem(0).GetCounts();
	ASSERT_TRUE(counts.known);
	EXPECT_EQ(counts.alive, 64u);
	EXPECT_EQ(counts.alive, Alive(watched));
	EXPECT_EQ(counts.dropped, 16u);
	EXPECT_FALSE(late->GetSystem(0).GetCounts().known);

	late->WatchCounts(true);
	for (u32 frame = 0; frame <= RHI::FRAMES_IN_FLIGHT; ++frame) {
		Frame(1.0f);
	}
	ASSERT_TRUE(late->GetSystem(0).GetCounts().known);
	EXPECT_EQ(late->GetSystem(0).GetCounts().alive, 64u);

	instance->SetPaused(false);
	watched->Reinit();
	for (u32 frame = 0; frame <= RHI::FRAMES_IN_FLIGHT; ++frame) {
		Frame(1.0f);
	}
	ASSERT_TRUE(instance->GetSystem(0).GetCounts().known);
	EXPECT_EQ(instance->GetSystem(0).GetCounts().alive, 64u);
	EXPECT_EQ(instance->GetSystem(0).GetCounts().dropped, 16u);
}

TEST_F(VisualEffectComponentFixture, SeedAndEachSpawnsIdReachKernels) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device";
	}
	VisualEffectAsset* asset = MakeAsset(EffectSpawnKind::SingleBurst, Burst(8.0f), "SeedInitialize.hlsl", 4);
	if (!asset) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Handle<VisualEffect> fixed = AddEffect(asset);
	fixed->startSeed = 4321;
	fixed->resetSeedOnPlay = false;
	Handle<VisualEffect> reseeding = AddEffect(asset);
	reseeding->startSeed = 4321;
	Frame(0.25f);

	EffectInstance* fixedInstance = EffectComponentAccess::Instance(*fixed.Get());
	EffectInstance* reseedingInstance = EffectComponentAccess::Instance(*reseeding.Get());
	ASSERT_NE(fixedInstance, nullptr);
	ASSERT_NE(reseedingInstance, nullptr);
	EXPECT_EQ(fixedInstance->GetSeed(), 4321u);
	EXPECT_NE(reseedingInstance->GetSeed(), 4321u);
	for (f32 seed : Attribute(fixed, 2)) {
		EXPECT_EQ(std::bit_cast<u32>(seed), 4321u);
	}
	Vector<f32> ids = Attribute(fixed, 3);
	std::sort(ids.begin(), ids.end());
	EXPECT_EQ(ids, (Vector<f32>{{0, 1, 2, 3, 4, 5, 6, 7}, GetMemoryResource(MemTag::Graphics)}));

	const u32 drawn = reseedingInstance->GetSeed();
	fixed->Play();
	reseeding->Play();
	Frame(0.25f);
	EXPECT_EQ(fixedInstance->GetSeed(), 4321u);
	EXPECT_NE(reseedingInstance->GetSeed(), drawn);
	EXPECT_EQ(reseeding->startSeed, 4321u);
	ids = Attribute(fixed, 3);
	std::sort(ids.begin(), ids.end());
	ASSERT_EQ(ids.size(), 16u);
	EXPECT_EQ(ids.back(), 15.0f);
}

#else

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef CreateDirectory
#undef far
#undef near

namespace {

	constexpr std::string_view TintedSpark = R"({
	"_type": "EffectGraph",
	"version": 2,
	"nextId": 20,
	"settings": {},
	"properties": [
		{"id":1,"name":"Tint","reference":"tint","type":"Color","default":[0,1,0,1]}
	],
	"nodes": [
		{"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}}
	],
	"contexts": [
		{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[
			{"id":3,"def":"Block/Burst","in":{"Count":{"value":[1]}}}
		]},
		{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":4},"flow":[[2,"In"]],"blocks":[
			{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,0,0.5]}}},
			{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[1]}}}
		]},
		{"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[4,"In"]],"blocks":[
			{"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[10,"Out"]}}}
		]}
	],
	"groups": [],
	"notes": []
}
)";

	class VisualEffectSceneFixture : public MaterialReadbackFixture {
	protected:
		ModuleHandle _module;

		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path("VisualEffects.dll"), ModuleLoadPolicy::InPlace);
			SetEffectTimes(0.0f, 0.0f);
		}

		void TearDown() override {
			SetEffectTimes(1.0f / 60.0f, 0.05f);
			ModuleSystem::Terminate();
			MaterialReadbackFixture::TearDown();
		}

		static bool SetEffectTimes(f32 fixedTimeStep, f32 maxDeltaTime) {
			Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
			Settings::GetCategories(categories);
			for (const SettingsCategoryInfo& info : categories) {
				const FieldInfo* step = info.path == "Effects" && info.live ? FindField(info.type, StringHash("fixedTimeStep")) : nullptr;
				const FieldInfo* most = step ? FindField(info.type, StringHash("maxDeltaTime")) : nullptr;
				if (step && most && step->addrFn && most->addrFn) {
					*static_cast<f32*>(step->addrFn(info.live)) = fixedTimeStep;
					*static_cast<f32*>(most->addrFn(info.live)) = maxDeltaTime;
					return true;
				}
			}
			return false;
		}
	};

} // anonymous namespace

TEST_F(VisualEffectSceneFixture, ImportedEffectDrawsInPlace) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteProjectFile("Spark.sleffect", str(TintedSpark));
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Spark.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	Handle<Component> component = object->AddComponent(type);
	ASSERT_TRUE(component.IsValid());
	VisualEffect* effect = static_cast<VisualEffect*>(component.Get());
	effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	FrameStages::Run(FrameStage::LateUpdate, {});
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	Vec3 center = CenterTexel(pixels, size);
	EXPECT_GT(center.y, 150.0f);
	EXPECT_LT(center.z, 40.0f);

	effect->properties.push_back({str("Tint"), Vec4(0.0f, 0.0f, 1.0f, 1.0f)});
	FrameStages::Run(FrameStage::LateUpdate, {});
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	center = CenterTexel(pixels, size);
	EXPECT_GT(center.z, 150.0f);
	EXPECT_LT(center.y, 40.0f);

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, EffectOutlinedAndPicked) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteProjectFile("Spark.sleffect", str(TintedSpark));
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Spark.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	Handle<Component> component = object->AddComponent(type);
	ASSERT_TRUE(component.IsValid());
	static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();
	FrameStages::Run(FrameStage::LateUpdate, {});

	auto redTexels = [](const Vector<u8>& pixels) {
		u32 count = 0;
		for (size_t at = 0; at + 3 < pixels.size(); at += 4) {
			count += pixels[at] > 200 && pixels[at + 1] < 60 && pixels[at + 2] < 60 ? 1u : 0u;
		}
		return count;
	};
	const ViewOutline outline{object, Color32(255, 0, 0, 255)};
	auto render = [&](bool outlined, const ViewPick* pick, Vector<u8>& pixels) {
		ViewDesc desc = QuadView();
		desc.tonemap = false;
		if (outlined) {
			desc.outlines = {&outline, 1};
			desc.outlineWidth = 3.0f;
		}
		desc.pick = pick;
		RenderSystem::SubmitView(stage.target, desc);
		RenderSystem::RenderAll();
		Vec2I size{};
		return RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
	};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(render(false, nullptr, pixels));
	EXPECT_EQ(redTexels(pixels), 0u);
	ASSERT_TRUE(render(true, nullptr, pixels));
	EXPECT_GT(redTexels(pixels), 30u);

	const Vec2I size = RenderSystem::GetViewTargetSize(stage.target);
	const ViewPick middle{Vec2I(size.x / 2, size.y / 2), 1};
	const ViewPick corner{Vec2I(1, 1), 2};
	ASSERT_TRUE(render(false, &middle, pixels));
	ASSERT_TRUE(render(false, &corner, pixels));
	PickAnswer answers[2];
	u32 taken = 0;
	for (u32 frame = 0; frame < 2 * RHI::FRAMES_IN_FLIGHT && taken < 2; ++frame) {
		while (taken < 2 && RenderSystem::TakePickAnswer(stage.target, answers[taken])) {
			++taken;
		}
		if (taken < 2) {
			ASSERT_TRUE(render(false, nullptr, pixels));
		}
	}
	ASSERT_EQ(taken, 2u);
	EXPECT_EQ(answers[0].tag, 1u);
	EXPECT_EQ(answers[0].object, object);
	EXPECT_EQ(answers[1].tag, 2u);
	EXPECT_FALSE(answers[1].object.IsValid());

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, OutputStatesHoldThroughChain) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlit, nullptr);
	MaterialAsset* red = LoadMaterial("Occluder.mat", unlit->assetId, "\t\"colors\": { \"baseColor\": [1, 0, 0, 1] }");
	ASSERT_NE(red, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(red, stage));
	stage.sun->SetEnabled(false);
	stage.quad->SetLocalPosition(Vec3(0.0f, 0.0f, 1.0f));
	SceneSystem::EndFrame();
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);

	struct Row {
		std::string_view settings;
		bool occluded;
		bool drawn;
	};
	const Row rows[] = {
		{"", true, false},
		{R"("Z Test Mode":"Always")", true, true},
		{R"("Z Test Mode":"Greater")", true, true},
		{R"("Z Test Mode":"Greater")", false, false},
		{"", false, true},
		{R"("Cull Mode":"Back")", false, true},
		{R"("Cull Mode":"Front")", false, false},
	};
	u32 index = 0;
	for (const Row& row : rows) {
		SCOPED_TRACE(std::format("settings {{{}}} {}", row.settings, row.occluded ? "behind the quad" : "alone"));
		str text(TintedSpark);
		constexpr std::string_view Output = R"("def":"Context/OutputParticlePrimitive","x":0,"y":600,)";
		ASSERT_NE(text.find(Output), str::npos);
		text.replace(text.find(Output), Output.size(), std::format(R"("def":"Context/OutputParticlePrimitive","x":0,"y":600,"settings":{{{}}},)", row.settings));
		constexpr std::string_view Tint = R"({"id":7,"def":"Block/SetAttribute")";
		text.replace(text.find(Tint), Tint.size(), str(R"({"id":8,"def":"Block/Orient"},)") + str(Tint));
		const str file = std::format("Spark{}.sleffect", index++);
		WriteProjectFile(file.c_str(), text);
		Asset* imported = AssetDatabase::LoadAsset(Path("project://" + file));
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		stage.quad->SetActive(row.occluded);
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(type).Get());
		ASSERT_NE(effect, nullptr);
		effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		FrameStages::Run(FrameStage::LateUpdate, {});
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		const Vec3 center = CenterTexel(pixels, size);
		if (row.drawn) {
			EXPECT_GT(center.y, 150.0f) << center.x << ", " << center.y << ", " << center.z;
			EXPECT_LT(center.x, 40.0f);
		} else {
			EXPECT_LT(center.y, 40.0f) << center.x << ", " << center.y << ", " << center.z;
			EXPECT_EQ(center.x > 150.0f, row.occluded);
		}
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		RenderSystem::GetDevice()->WaitIdle();
		AssetDatabase::UnloadAsset(imported);
	}
	TearDownStage(stage);
}

TEST_F(VisualEffectSceneFixture, AutoBoxFollowsParticle) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteProjectFile("Spark.sleffect", str(TintedSpark));
	Asset* control = AssetDatabase::LoadAsset(Path("project://Spark.sleffect"));
	if (!control) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	AssetDatabase::UnloadAsset(control);
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	for (const std::string_view target : {std::string_view("GPU"), std::string_view("CPU")}) {
		SCOPED_TRACE(target);
		const str file = "Drift" + str(target) + ".sleffect";
		WriteProjectFile(file.c_str(), std::format(R"({{"_type":"EffectGraph","version":2,"nextId":20,"settings":{{}},"properties":[],"nodes":[],
"contexts":[
{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[1]}}}}}}]}},
{{"id":4,"def":"Context/Initialize","settings":{{"Capacity":4,"Space":"World","Bounds Setting Mode":"Automatic","Simulation Target":"{}"}},"flow":[[2,"In"]],"blocks":[
{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[4,0,0.5],"space":"World"}}}}}},
{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[1]}}}}}},
{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"value":[-1,0,0]}}}}}},
{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"value":[0,1,0]}}}}}}]}},
{{"id":7,"def":"Context/Update","flow":[[4,"In"]],"blocks":[]}},
{{"id":9,"def":"Context/OutputParticlePrimitive","flow":[[7,"In"]],"blocks":[]}}],
"groups":[],"notes":[]}})",
												   target));
		Asset* imported = AssetDatabase::LoadAsset(Path("project://" + file));
		ASSERT_NE(imported, nullptr);
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect"))).Get());
		ASSERT_NE(effect, nullptr);
		effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		object->SetLocalPosition(Vec3(50.0f, 0.0f, 0.0f));
		SceneSystem::EndFrame();

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		bool reached = false;
		bool culled = false;
		for (u32 frame = 0; frame < 60 && !reached; ++frame) {
			Time::BeginFrame(0.1);
			FrameStages::Run(FrameStage::LateUpdate, {});
			ASSERT_TRUE(RenderStage(stage, pixels, size));
			culled = culled || effect->IsCulled();
			const Vec3 center = CenterTexel(pixels, size);
			reached = center.y > 150.0f && center.x < 60.0f;
		}
		Time::ResetFrameClock();
		EXPECT_TRUE(culled);
		EXPECT_TRUE(reached);
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		AssetDatabase::UnloadAsset(imported);
	}
	TearDownStage(stage);
}

TEST_F(VisualEffectSceneFixture, CpuEffectMatchesGpuTwin) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);

	Vec3 drawn[2][2];
	for (const bool cpu : {false, true}) {
		str text(TintedSpark);
		constexpr std::string_view Capacity = R"("settings":{"Capacity":4})";
		ASSERT_NE(text.find(Capacity), str::npos);
		text.replace(text.find(Capacity), Capacity.size(), cpu ? R"("settings":{"Capacity":4,"Simulation Target":"CPU"})" : Capacity);
		const char* file = cpu ? "SparkCpu.sleffect" : "SparkGpu.sleffect";
		WriteProjectFile(file, text);
		Asset* imported = AssetDatabase::LoadAsset(Path(str("project://") + file));
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(type).Get());
		ASSERT_NE(effect, nullptr);
		effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		FrameStages::Run(FrameStage::LateUpdate, {});
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		drawn[cpu][0] = CenterTexel(pixels, size);
		effect->properties.push_back({str("Tint"), Vec4(0.0f, 0.0f, 1.0f, 1.0f)});
		FrameStages::Run(FrameStage::LateUpdate, {});
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		drawn[cpu][1] = CenterTexel(pixels, size);
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		RenderSystem::GetDevice()->WaitIdle();
		AssetDatabase::UnloadAsset(imported);
	}
	EXPECT_GT(drawn[0][0].y, 150.0f);
	EXPECT_GT(drawn[0][1].z, 150.0f);
	for (u32 frame = 0; frame < 2; ++frame) {
		EXPECT_EQ(drawn[1][frame].x, drawn[0][frame].x);
		EXPECT_EQ(drawn[1][frame].y, drawn[0][frame].y) << "frame " << frame;
		EXPECT_EQ(drawn[1][frame].z, drawn[0][frame].z) << "frame " << frame;
	}
	TearDownStage(stage);
}

TEST_F(VisualEffectSceneFixture, CpuEffectsSimulateApart) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	str text(TintedSpark);
	constexpr std::string_view Capacity = R"("settings":{"Capacity":4})";
	ASSERT_NE(text.find(Capacity), str::npos);
	text.replace(text.find(Capacity), Capacity.size(), R"("settings":{"Capacity":4,"Simulation Target":"CPU"})");
	WriteProjectFile("SparkCpu.sleffect", text);
	Asset* imported = AssetDatabase::LoadAsset(Path("project://SparkCpu.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);
	Handle<SceneObject> objects[2];
	for (Handle<SceneObject>& object : objects) {
		object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(type).Get());
		ASSERT_NE(effect, nullptr);
		effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	}
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	FrameStages::Run(FrameStage::LateUpdate, {});
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);
	for (u32 dying = 0; dying < 2; ++dying) {
		FrameStages::Run(FrameStage::LateUpdate, {});
		SceneSystem::Destroy(objects[dying]);
		SceneSystem::EndFrame();
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		FrameStages::Run(FrameStage::LateUpdate, {});
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		if (dying == 0) {
			EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);
		} else {
			EXPECT_LT(CenterTexel(pixels, size).y, 40.0f);
		}
	}
	RenderSystem::GetDevice()->WaitIdle();
	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, ScriptReadsCpuPositions) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	constexpr std::string_view Rising = R"({"_type":"EffectGraph","version":4,"nextId":20,"settings":{},"properties":[],"nodes":[],
"contexts":[
	{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[{"id":3,"def":"Block/Burst","in":{"Count":{"value":[4]}}}]},
	{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":8,"Simulation Target":"CPU","System Name":"Sparks"},"flow":[[2,"In"]],"blocks":[
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,2,0]}}},
		{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}}]},
	{"id":7,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},
	{"id":8,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[7,"In"]],"blocks":[]},
	{"id":10,"def":"Context/Initialize","x":300,"y":200,"settings":{"Capacity":8,"Simulation Target":"CPU"},"flow":[[2,"In"]],"blocks":[]},
	{"id":11,"def":"Context/OutputParticlePrimitive","x":300,"y":600,"flow":[[10,"In"]],"blocks":[]},
	{"id":12,"def":"Context/Initialize","x":600,"y":200,"settings":{"Capacity":8,"System Name":"Smoke"},"flow":[[2,"In"]],"blocks":[]},
	{"id":13,"def":"Context/OutputParticlePrimitive","x":600,"y":600,"flow":[[12,"In"]],"blocks":[]}],
"groups":[],"notes":[]})";
	WriteProjectFile("Rising.sleffect", str(Rising));
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Rising.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	ShadingStage stage;
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(type).Get());
	ASSERT_NE(effect, nullptr);
	effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();

	FrameTime clock;
	clock.deltaTime = 0.25f;
	clock.unscaledDeltaTime = 0.25f;
	clock.maximumDeltaTime = 1000.0f;
	Time::FrameScope scope{clock};
	FrameStages::Run(FrameStage::LateUpdate, {});
	RenderSystem::RenderAll();
	FrameStages::Run(FrameStage::LateUpdate, {});
	RenderSystem::RenderAll();

	const ModuleHandle script = ModuleSystem::Load(Path("ShalltearVisualEffectsScriptModule.dll"));
	ASSERT_TRUE(ModuleSystem::IsLoaded(script));
	HMODULE image = static_cast<HMODULE>(ModuleSystem::GetNativeHandle(script));
	using CountFn = u32 (*)(VisualEffect*, const char*);
	using ReadFn = u32 (*)(VisualEffect*, const char*, f32*, u32);
	const CountFn count = image ? reinterpret_cast<CountFn>(GetProcAddress(image, "VisualEffectsScriptModuleCount")) : nullptr;
	const ReadFn read = image ? reinterpret_cast<ReadFn>(GetProcAddress(image, "VisualEffectsScriptModuleReadPositions")) : nullptr;
	EXPECT_TRUE(count && read);
	if (count && read) {
		f32 xyz[3 * 8] = {};
		EXPECT_EQ(count(effect, "Sparks"), 4u);
		EXPECT_EQ(read(effect, "Sparks", xyz, 8), 4u);
		for (u32 i = 0; i < 4; ++i) {
			EXPECT_EQ(Vec3(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]), Vec3(0.0f, 0.5f, 0.0f)) << "particle " << i;
		}
		EXPECT_EQ(read(effect, "Sparks", xyz, 3), 3u);
		EXPECT_EQ(count(effect, "System"), 4u);
		EXPECT_EQ(read(effect, "System", xyz, 8), 0u);
		EXPECT_EQ(count(effect, "Smoke"), 0u);
	}
	ModuleSystem::Unload(script);

	SceneSystem::Destroy(object);
	SceneSystem::EndFrame();
	RenderSystem::GetDevice()->WaitIdle();
	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, EffectUsesProjectFixedStep) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	ASSERT_TRUE(SetEffectTimes(1.0f / 60.0f, 0.05f));
	constexpr std::string_view Rising = R"({"_type":"EffectGraph","version":4,"nextId":20,"settings":{"exactFixedTime":true},"properties":[],"nodes":[],
"contexts":[
	{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[{"id":3,"def":"Block/Burst","in":{"Count":{"value":[4]}}}]},
	{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":8,"Simulation Target":"CPU","System Name":"Sparks"},"flow":[[2,"In"]],"blocks":[
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,2,0]}}},
		{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}}]},
	{"id":7,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},
	{"id":8,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[7,"In"]],"blocks":[]}],
"groups":[],"notes":[]})";
	str unscaled(Rising);
	constexpr std::string_view Exact = R"("settings":{"exactFixedTime":true})";
	unscaled.replace(unscaled.find(Exact), Exact.size(), R"("settings":{"exactFixedTime":true,"ignoreTimeScale":true})");
	WriteProjectFile("Scaled.sleffect", str(Rising));
	WriteProjectFile("Unscaled.sleffect", unscaled);
	Asset* scaled = AssetDatabase::LoadAsset(Path("project://Scaled.sleffect"));
	Asset* wall = AssetDatabase::LoadAsset(Path("project://Unscaled.sleffect"));
	if (!scaled || !wall) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	ShadingStage stage;
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);
	VisualEffect* effects[2] = {};
	Asset* assets[2] = {scaled, wall};
	for (u32 i = 0; i < 2; ++i) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		effects[i] = static_cast<VisualEffect*>(object->AddComponent(type).Get());
		ASSERT_NE(effects[i], nullptr);
		effects[i]->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(assets[i]));
	}
	SceneSystem::EndFrame();

	FrameTime clock;
	clock.deltaTime = 0.0f;
	clock.unscaledDeltaTime = 0.25f;
	clock.timeScale = 0.0f;
	Time::FrameScope scope{clock};
	for (u32 frame = 0; frame < 2; ++frame) {
		FrameStages::Run(FrameStage::LateUpdate, {});
		RenderSystem::RenderAll();
	}

	const ModuleHandle script = ModuleSystem::Load(Path("ShalltearVisualEffectsScriptModule.dll"));
	ASSERT_TRUE(ModuleSystem::IsLoaded(script));
	HMODULE image = static_cast<HMODULE>(ModuleSystem::GetNativeHandle(script));
	using ReadFn = u32 (*)(VisualEffect*, const char*, f32*, u32);
	const ReadFn read = image ? reinterpret_cast<ReadFn>(GetProcAddress(image, "VisualEffectsScriptModuleReadPositions")) : nullptr;
	ASSERT_NE(read, nullptr);
	f32 xyz[3 * 8] = {};
	ASSERT_EQ(read(effects[0], "Sparks", xyz, 8), 4u);
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_EQ(xyz[3 * i + 1], 0.0f);
	}
	ASSERT_EQ(read(effects[1], "Sparks", xyz, 8), 4u);
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_NEAR(xyz[3 * i + 1], 2.0f * 5.0f / 60.0f, 1e-5f);
	}
	ModuleSystem::Unload(script);

	RenderSystem::GetDevice()->WaitIdle();
	TearDownStage(stage);
	AssetDatabase::UnloadAsset(scaled);
	AssetDatabase::UnloadAsset(wall);
}

TEST_F(VisualEffectSceneFixture, CpuSystemUsesMeshVertices) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	constexpr std::string_view Cornered = R"({"_type":"EffectGraph","version":4,"nextId":40,"settings":{},"properties":[],"nodes":[
	{"id":30,"def":"Sampling/MeshVertexCount"},
	{"id":31,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
	{"id":32,"def":"Sampling/SampleMesh","in":{"Vertex":{"from":[31,"spawnIndex"]}}}],
"contexts":[
	{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[{"id":3,"def":"Block/Burst","in":{"Count":{"from":[30,"Count"]}}}]},
	{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":64,"Simulation Target":"CPU","System Name":"Corners"},"flow":[[2,"In"]],"blocks":[
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[32,"Position"]}}},
		{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}}]},
	{"id":7,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},
	{"id":8,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[7,"In"]],"blocks":[]}],
"groups":[],"notes":[]})";
	WriteProjectFile("Cornered.sleffect", str(Cornered));
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Cornered.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	const MeshAsset* cube = static_cast<const MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube))));
	ASSERT_NE(cube, nullptr);
	ASSERT_GT(cube->vertices.size(), 8u);
	ShadingStage stage;
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect"))).Get());
	ASSERT_NE(effect, nullptr);
	effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();

	FrameTime clock;
	clock.deltaTime = 0.25f;
	clock.unscaledDeltaTime = 0.25f;
	clock.maximumDeltaTime = 1000.0f;
	Time::FrameScope scope{clock};
	FrameStages::Run(FrameStage::LateUpdate, {});
	RenderSystem::RenderAll();
	FrameStages::Run(FrameStage::LateUpdate, {});
	RenderSystem::RenderAll();

	const ModuleHandle script = ModuleSystem::Load(Path("ShalltearVisualEffectsScriptModule.dll"));
	ASSERT_TRUE(ModuleSystem::IsLoaded(script));
	HMODULE image = static_cast<HMODULE>(ModuleSystem::GetNativeHandle(script));
	using CountFn = u32 (*)(VisualEffect*, const char*);
	using ReadFn = u32 (*)(VisualEffect*, const char*, f32*, u32);
	const CountFn count = image ? reinterpret_cast<CountFn>(GetProcAddress(image, "VisualEffectsScriptModuleCount")) : nullptr;
	const ReadFn read = image ? reinterpret_cast<ReadFn>(GetProcAddress(image, "VisualEffectsScriptModuleReadPositions")) : nullptr;
	EXPECT_TRUE(count && read);
	if (count && read) {
		const u32 corners = static_cast<u32>(cube->vertices.size());
		EXPECT_EQ(count(effect, "Corners"), corners);
		f32 xyz[3 * 64] = {};
		const u32 got = read(effect, "Corners", xyz, 64);
		EXPECT_EQ(got, corners);
		u32 away = 0;
		for (u32 i = 0; i < std::min(got, corners); ++i) {
			away += Vec3(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]) == cube->vertices[i].position ? 0u : 1u;
		}
		EXPECT_EQ(away, 0u);
	}
	ModuleSystem::Unload(script);

	SceneSystem::Destroy(object);
	SceneSystem::EndFrame();
	RenderSystem::GetDevice()->WaitIdle();
	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, ScriptHearsImportedCpuSystemsParticlesDie) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	constexpr std::string_view Dying = R"({"_type":"EffectGraph","version":4,"nextId":20,"settings":{},"properties":[],"nodes":[],
"contexts":[
	{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[{"id":3,"def":"Block/Burst","in":{"Count":{"value":[4]}}}]},
	{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":8,"Simulation Target":"CPU"},"flow":[[2,"In"]],"blocks":[
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,2,0]}}},
		{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[0.5]}}}]},
	{"id":7,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[
		{"id":12,"def":"Block/TriggerEvent","settings":{"Mode":"OnDie"},"in":{"Count":{"value":[1]}}}]},
	{"id":8,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[7,"In"]],"blocks":[]},
	{"id":9,"def":"Context/OutputEvent","x":300,"y":400,"settings":{"Event Name":"Died"},"in":{"Evt":{"from":[12,"Evt"]}},"blocks":[]}],
"groups":[],"notes":[]})";
	WriteProjectFile("Dying.sleffect", str(Dying));
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Dying.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	const ModuleHandle script = ModuleSystem::Load(Path("ShalltearVisualEffectsScriptModule.dll"));
	ASSERT_TRUE(ModuleSystem::IsLoaded(script));
	HMODULE image = static_cast<HMODULE>(ModuleSystem::GetNativeHandle(script));
	using HeardFn = u32 (*)(Component*, char*, u32, f32*, f32*);
	const HeardFn heard = image ? reinterpret_cast<HeardFn>(GetProcAddress(image, "VisualEffectsScriptModuleHeard")) : nullptr;
	const TypeInfo* effectType = TypeRegistry::FindType(StringHash("VisualEffect"));
	const TypeInfo* probeType = TypeRegistry::FindType(StringHash("ParticleEventProbe"));
	EXPECT_TRUE(heard && effectType && probeType);

	ShadingStage stage;
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	const bool staged = BuildStage(&anyMaterial, stage);
	EXPECT_TRUE(staged);
	if (heard && effectType && probeType && staged) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(effectType).Get());
		Component* probe = object->AddComponent(probeType).Get();
		EXPECT_TRUE(effect && probe);
		if (effect && probe) {
			effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
			SceneSystem::EndFrame();
			FrameTime clock;
			clock.deltaTime = 0.25f;
			clock.unscaledDeltaTime = 0.25f;
			clock.maximumDeltaTime = 1000.0f;
			Time::FrameScope scope{clock};
			char name[32] = {};
			f32 position[3] = {};
			f32 counted = 0.0f;
			for (u32 frame = 1; frame <= 4; ++frame) {
				FrameStages::Run(FrameStage::LateUpdate, {});
				RenderSystem::RenderAll();
				if (frame < 4) {
					EXPECT_EQ(heard(probe, name, sizeof(name), position, &counted), 0u) << "frame " << frame;
				}
			}
			EXPECT_EQ(heard(probe, name, sizeof(name), position, &counted), 4u);
			EXPECT_STREQ(name, "Died");
			EXPECT_EQ(position[1], 1.5f);
			EXPECT_EQ(counted, 4.0f);
		}
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		RenderSystem::GetDevice()->WaitIdle();
	}
	if (staged) {
		TearDownStage(stage);
	}
	ModuleSystem::Unload(script);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, ScriptHearsImportedBurst) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	constexpr std::string_view Bursting = R"({"_type":"EffectGraph","version":4,"nextId":20,"settings":{},"properties":[],"nodes":[],
"contexts":[
	{"id":1,"def":"Context/Event","x":0,"y":0,"settings":{"Event Name":"Hit"},"blocks":[]},
	{"id":2,"def":"Context/Spawn","x":0,"y":200,"flow":[[1,"Start"]],"blocks":[{"id":3,"def":"Block/Burst","in":{"Count":{"value":[3]}}}]},
	{"id":4,"def":"Context/OutputEvent","x":300,"y":0,"settings":{"Event Name":"Echo"},"flow":[[1,"In"]],"blocks":[]},
	{"id":5,"def":"Context/OutputEvent","x":0,"y":400,"settings":{"Event Name":"Burst"},"flow":[[2,"In"]],"blocks":[]}],
"groups":[],"notes":[]})";
	WriteProjectFile("Bursting.sleffect", str(Bursting));
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Bursting.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	const ModuleHandle script = ModuleSystem::Load(Path("ShalltearVisualEffectsScriptModule.dll"));
	ASSERT_TRUE(ModuleSystem::IsLoaded(script));
	HMODULE image = static_cast<HMODULE>(ModuleSystem::GetNativeHandle(script));
	using HeardFn = u32 (*)(Component*, char*, u32, f32*, f32*);
	const HeardFn heard = image ? reinterpret_cast<HeardFn>(GetProcAddress(image, "VisualEffectsScriptModuleHeard")) : nullptr;
	const TypeInfo* effectType = TypeRegistry::FindType(StringHash("VisualEffect"));
	const TypeInfo* probeType = TypeRegistry::FindType(StringHash("ParticleEventProbe"));
	EXPECT_TRUE(heard && effectType && probeType);

	ShadingStage stage;
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	const bool staged = BuildStage(&anyMaterial, stage);
	EXPECT_TRUE(staged);
	if (heard && effectType && probeType && staged) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(effectType).Get());
		Component* probe = object->AddComponent(probeType).Get();
		EXPECT_TRUE(effect && probe);
		if (effect && probe) {
			effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
			effect->initialEventName = "Hit";
			SceneSystem::EndFrame();
			FrameTime clock;
			clock.deltaTime = 0.25f;
			clock.unscaledDeltaTime = 0.25f;
			clock.maximumDeltaTime = 1000.0f;
			Time::FrameScope scope{clock};
			char name[32] = {};
			f32 position[3] = {};
			f32 counted = 0.0f;
			FrameStages::Run(FrameStage::LateUpdate, {});
			RenderSystem::RenderAll();
			EXPECT_EQ(heard(probe, name, sizeof(name), position, &counted), 2u);
			EXPECT_STREQ(name, "Burst");
			EXPECT_EQ(counted, 3.0f);
			FrameStages::Run(FrameStage::LateUpdate, {});
			RenderSystem::RenderAll();
			EXPECT_EQ(heard(probe, name, sizeof(name), position, &counted), 2u);
		}
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		RenderSystem::GetDevice()->WaitIdle();
	}
	if (staged) {
		TearDownStage(stage);
	}
	ModuleSystem::Unload(script);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, InitialEventSpawnsOne) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	str graph(TintedSpark);
	constexpr std::string_view Burst = R"({"id":3,"def":"Block/Burst","in":{"Count":{"value":[1]}}})";
	constexpr std::string_view Spawn = R"("def":"Context/Spawn")";
	ASSERT_NE(graph.find(Burst), str::npos);
	graph.erase(graph.find(Burst), Burst.size());
	graph.replace(graph.find(Spawn), Spawn.size(), R"("def":"Context/Event")");
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);

	for (const bool linked : {true, false}) {
		str text = graph;
		if (!linked) {
			constexpr std::string_view Flow = R"("flow":[[2,"In"]],)";
			text.erase(text.find(Flow), Flow.size());
		}
		const char* file = linked ? "Linked.sleffect" : "Unlinked.sleffect";
		WriteProjectFile(file, text);
		Asset* imported = AssetDatabase::LoadAsset(Path(str("project://") + file));
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(type).Get());
		ASSERT_NE(effect, nullptr);
		effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		FrameStages::Run(FrameStage::LateUpdate, {});
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		const Vec3 center = CenterTexel(pixels, size);
		if (linked) {
			EXPECT_GT(center.y, 150.0f);
		} else {
			EXPECT_LT(center.y, 40.0f);
		}
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		RenderSystem::GetDevice()->WaitIdle();
		AssetDatabase::UnloadAsset(imported);
	}
	TearDownStage(stage);
}

TEST_F(VisualEffectSceneFixture, SystemSubgraphDrawsInUser) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteProjectFile("Spark.sleffect", str(TintedSpark));
	const Guid spark = AssetDatabase::EnsureAssetIdentity(Path("project://Spark.sleffect"));
	ASSERT_TRUE(spark.IsValid());
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffect"));
	ASSERT_NE(type, nullptr);

	struct Case {
		bool fired;
		const char* initial;
		bool drawn;
	};
	for (const Case& run : {Case{false, "OnPlay", true}, Case{true, "OnPlay", false}, Case{true, "Fire", true}}) {
		const str user = std::format(
			R"({{"_type":"EffectGraph","version":4,"nextId":20,"properties":[],"nodes":[],"contexts":[{}
{{"id":1,"def":"Context/Subgraph","settings":{{"asset":"{}"}},"in":{{"1":{{"value":[0,0,1,1]}}}}{},"blocks":[]}}]}})",
			run.fired ? R"({"id":2,"def":"Context/Event","settings":{"Event Name":"Fire"},"blocks":[]},)" : "", spark.ToString(),
			run.fired ? R"(,"flow":[[2,"OnPlay"]])" : "");
		const char* file = run.fired ? "Fired.sleffect" : "User.sleffect";
		WriteProjectFile(file, user);
		Asset* imported = AssetDatabase::LoadAsset(Path(str("project://") + file));
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		VisualEffect* effect = static_cast<VisualEffect*>(object->AddComponent(type).Get());
		ASSERT_NE(effect, nullptr);
		effect->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		effect->initialEventName = run.initial;
		SceneSystem::EndFrame();

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		FrameStages::Run(FrameStage::LateUpdate, {});
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		const Vec3 center = CenterTexel(pixels, size);
		if (run.drawn) {
			EXPECT_GT(center.z, 150.0f);
			EXPECT_LT(center.y, 40.0f) << file;
		} else {
			EXPECT_LT(center.z, 40.0f);
		}
		SceneSystem::Destroy(object);
		SceneSystem::EndFrame();
		RenderSystem::GetDevice()->WaitIdle();
		AssetDatabase::UnloadAsset(imported);
	}
	TearDownStage(stage);
}

TEST_F(VisualEffectSceneFixture, AParticleItsOutputKillsDrawsNothing) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	str graph(TintedSpark);
	const std::string_view block = R"({"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[10,"Out"]}}})";
	graph.replace(graph.find(block), block.size(),
				  str(block) + R"(,{"id":8,"def":"Block/SetAttribute","settings":{"Attribute":"alive"},"in":{"Alive":{"value":false}}})");
	WriteProjectFile("Killed.sleffect", graph);
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Killed.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
	ASSERT_TRUE(component.IsValid());
	static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	FrameStages::Run(FrameStage::LateUpdate, {});
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_LT(CenterTexel(pixels, size).y, 40.0f);

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, DepthCollisionStopsSpark) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	str graph(TintedSpark);
	const std::string_view placed = R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,0,0.5]}}})";
	graph.replace(graph.find(placed), placed.size(),
				  R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0.3,0,-1.8]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,0,5]}}})");
	const std::string_view output = R"({"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[4,"In"]],)";
	graph.replace(graph.find(output), output.size(),
				  R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[
			{"id":12,"def":"Block/CollisionDepth","settings":{"Radius Mode":"Custom"},"in":{"Bounce":{"value":[0]},"Particle Radius":{"value":[0.2]}}}]},
		{"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[8,"In"]],)");
	const std::string_view size = R"("in":{"Size":{"value":[1]}})";
	graph.replace(graph.find(size), size.size(), R"("in":{"Size":{"value":[0.3]}})");
	WriteProjectFile("DepthSpark.sleffect", graph);
	Asset* imported = AssetDatabase::LoadAsset(Path("project://DepthSpark.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset wallMaterial;
	wallMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));

	const f32 halfHeight = std::tan(30.0f * Math::DEG_TO_RAD);
	const f32 stoppedX = 0.3f * (4.0f / 4.2f) * (1.0f - 0.2f / 4.0f) / (3.8f * halfHeight) * 0.5f + 0.5f;
	const f32 spawnedX = 0.3f / (1.2f * halfHeight) * 0.5f + 0.5f;
	const auto texelAt = [](const Vector<u8>& pixels, Vec2I extent, f32 x) {
		const size_t index = (static_cast<size_t>(extent.y / 2) * extent.x + static_cast<size_t>(x * static_cast<f32>(extent.x))) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	};
	const auto middle = [&](bool namesCamera, Vec3& atSpawn) {
		ShadingStage stage;
		if (!BuildStage(&wallMaterial, stage)) {
			ADD_FAILURE() << "no stage";
			return Vec3();
		}
		stage.quad->SetLocalPosition(Vec3(0.0f, 0.0f, 1.0f));
		stage.quad->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
		stage.quad->SetLocalScale(Vec3(20.0f, 20.0f, 1.0f));
		Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", stage.scene);
		cameraObject->SetLocalPosition(Vec3(0.0f, 0.0f, -3.0f));
		Handle<Camera> camera = cameraObject->AddComponent<Camera>();
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
		static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I extent{};
		for (u32 frame = 0; frame < 12; ++frame) {
			Time::BeginFrame(0.1);
			FrameStages::Run(FrameStage::LateUpdate, {});
			ViewDesc desc = QuadView();
			desc.view = cameraObject->GetWorldMatrix().Inverse();
			desc.cameraPosition = cameraObject->GetWorldPosition();
			desc.camera = namesCamera ? camera : Handle<Camera>{};
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
		}
		Time::ResetFrameClock();
		const bool drawn = RenderSystem::ReadbackViewTarget(stage.target, pixels, extent);
		EXPECT_TRUE(drawn);
		const Vec3 texel = drawn ? texelAt(pixels, extent, stoppedX) : Vec3();
		atSpawn = drawn ? texelAt(pixels, extent, spawnedX) : Vec3();
		TearDownStage(stage);
		return texel;
	};
	Vec3 atSpawn;
	const Vec3 through = middle(false, atSpawn);
	EXPECT_GT(through.x, 150.0f);
	const Vec3 stopped = middle(true, atSpawn);
	EXPECT_LT(stopped.x, 60.0f);
	EXPECT_GT(stopped.y, 150.0f);
	EXPECT_GT(atSpawn.x, 150.0f);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, MeshCountDrivesSpawn) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	str graph(TintedSpark);
	const std::string_view property = R"({"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}})";
	graph.replace(graph.find(property), property.size(), str(property) + R"(,
		{"id":30,"def":"Sampling/MeshVertexCount"},
		{"id":31,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
		{"id":32,"def":"Math/Add","in":{"A":{"from":[31,"spawnIndex"]},"B":{"value":[1,0,0,0]}}},
		{"id":33,"def":"Math/Subtract","in":{"A":{"from":[32,"Out"]},"B":{"from":[30,"Count"]}}},
		{"id":34,"def":"Math/Multiply","in":{"A":{"from":[33,"Out"]},"B":{"value":[0.8,0,0,0]}}},
		{"id":35,"def":"Input/Vector3","in":{"X":{"from":[34,"Out"]},"Z":{"value":[0.5]}}})");
	const std::string_view burst = R"({"id":3,"def":"Block/Burst","in":{"Count":{"value":[1]}}})";
	graph.replace(graph.find(burst), burst.size(), R"({"id":3,"def":"Block/Burst","in":{"Count":{"from":[30,"Count"]}}})");
	const std::string_view placed = R"("in":{"Position":{"value":[0,0,0.5]}})";
	graph.replace(graph.find(placed), placed.size(), R"("in":{"Position":{"from":[35,"Out"]}})");
	const std::string_view size = R"("in":{"Size":{"value":[1]}})";
	graph.replace(graph.find(size), size.size(), R"("in":{"Size":{"value":[0.3]}})");
	const std::string_view capacity = R"("settings":{"Capacity":4})";
	graph.replace(graph.find(capacity), capacity.size(), R"("settings":{"Capacity":64})");
	WriteProjectFile("Counted.sleffect", graph);
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Counted.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
	ASSERT_TRUE(component.IsValid());
	static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I extent{};
	FrameStages::Run(FrameStage::LateUpdate, {});
	ASSERT_TRUE(RenderStage(stage, pixels, extent));
	const f32 side = 0.8f / (2.5f * std::tan(30.0f * Math::DEG_TO_RAD)) * 0.5f;
	const auto lit = [&](f32 x) {
		const size_t index = (static_cast<size_t>(extent.y / 2) * extent.x + static_cast<size_t>(x * static_cast<f32>(extent.x))) * 4;
		return pixels[index + 1] > 150;
	};
	EXPECT_TRUE(lit(0.5f));
	EXPECT_NE(lit(0.5f - side), lit(0.5f + side));

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectSceneFixture, GpuEventChildAtParent) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	str graph(TintedSpark);
	const std::string_view output = R"({"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[4,"In"]],)";
	graph.replace(graph.find(output), output.size(),
				  R"({"id":12,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[
			{"id":13,"def":"Block/TriggerEvent","settings":{"Mode":"Always"}}]},
		{"id":14,"def":"Context/GPUEvent","x":300,"y":400,"in":{"Evt":{"from":[13,"Evt"]}},"blocks":[]},
		{"id":15,"def":"Context/Initialize","x":300,"y":500,"settings":{"Capacity":8},"flow":[[14,"In"]],"blocks":[
			{"id":16,"def":"Block/SetAttribute","settings":{"Attribute":"position","Source":"Source"}},
			{"id":17,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[1]}}}]},
		{"id":9,"def":"Context/OutputParticlePrimitive","x":300,"y":700,"flow":[[15,"In"]],)");
	WriteProjectFile("Events.sleffect", graph);
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Events.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
	ASSERT_TRUE(component.IsValid());
	static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I extent{};
	FrameStages::Run(FrameStage::LateUpdate, {});
	ASSERT_TRUE(RenderStage(stage, pixels, extent));
	EXPECT_LT(CenterTexel(pixels, extent).y, 40.0f);
	FrameStages::Run(FrameStage::LateUpdate, {});
	ASSERT_TRUE(RenderStage(stage, pixels, extent));
	const Vec3 center = CenterTexel(pixels, extent);
	EXPECT_GT(center.y, 150.0f);
	EXPECT_LT(center.z, 40.0f);

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

class VisualEffectOrientFixture : public VisualEffectSceneFixture {
protected:
	Asset* Import(const char* name, std::string_view initialize, std::string_view output) {
		str graph(TintedSpark);
		const std::string_view color = R"({"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[10,"Out"]}}})";
		graph.replace(graph.find(color), color.size(), str(color) + (output.empty() ? str() : "," + str(output)));
		const std::string_view size = R"({"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[1]}}})";
		graph.replace(graph.find(size), size.size(), str(size) + (initialize.empty() ? str() : "," + str(initialize)));
		WriteProjectFile(name, graph);
		return AssetDatabase::LoadAsset(Path(str("project://") + name));
	}

	bool Lit(Asset* imported, Vec3 eye, bool& drawn) { return Middle(imported, eye, drawn).y > 150.0f; }

	Vec3 Middle(Asset* imported, Vec3 eye, bool& drawn) {
		MaterialAsset anyMaterial;
		anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
		ShadingStage stage;
		if (!BuildStage(&anyMaterial, stage)) {
			ADD_FAILURE() << "no stage";
			return {};
		}
		stage.quad->SetActive(false);
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
		static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();
		FrameStages::Run(FrameStage::LateUpdate, {});
		ViewDesc desc = QuadView();
		desc.view = Mat4::LookAt(eye, Vec3(0.0f, 0.0f, 0.5f));
		desc.cameraPosition = eye;
		RenderSystem::SubmitView(stage.target, desc);
		RenderSystem::RenderAll();
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		drawn = RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
		const Vec3 middle = drawn ? CenterTexel(pixels, size) : Vec3();
		TearDownStage(stage);
		return middle;
	}
};

TEST_F(VisualEffectOrientFixture, QuadFacesCameraByOrientAlone) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const Vec3 side(3.0f, 0.0f, 0.5f);
	struct Case {
		const char* name;
		std::string_view initialize;
		std::string_view output;
		bool lit;
	};
	const Case cases[] = {
		{"NoOrient", "", "", false},
		{"FaceCameraPlane", "", R"({"id":8,"def":"Block/Orient"})", true},
		{"FaceCameraPosition", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"FaceCameraPosition"}})", true},
		{"LookAtCamera", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"LookAtPosition"},"in":{"Position":{"value":[3,0,0.5]}}})", true},
		{"LookAway", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"LookAtPosition"},"in":{"Position":{"value":[0,0,10]}}})", false},
		{"LookAtLine", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"LookAtLine"},"in":{"Start":{"value":[3,0,0]},"End":{"value":[3,0,1]}}})", true},
		{"AdvancedFacing", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"Advanced"},"in":{"Axis Z":{"value":[-1,0,0]}}})", true},
		{"AdvancedEdgeOn", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"Advanced"}})", false},
		{"FixedUp", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"FixedAxis"}})", true},
		{"FixedAlongView", "", R"({"id":8,"def":"Block/Orient","settings":{"Mode":"FixedAxis"},"in":{"Up":{"value":[1,0,0]}}})", false},
		{"AlongVelocity", R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,2,0]}}})",
		 R"({"id":8,"def":"Block/Orient","settings":{"Mode":"AlongVelocity"}})", true},
		{"AlongVelocityTowardCamera", R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[2,0,0]}}})",
		 R"({"id":8,"def":"Block/Orient","settings":{"Mode":"AlongVelocity"}})", false},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		Asset* imported = Import((str("Orient") + entry.name + ".sleffect").c_str(), entry.initialize, entry.output);
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		bool drawn = false;
		const bool lit = Lit(imported, side, drawn);
		ASSERT_TRUE(drawn);
		EXPECT_EQ(lit, entry.lit) << (entry.lit ? "the Orient did not turn the quad to face the camera" : "the quad faced a camera it was turned away from");
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectOrientFixture, QuadUsesPivotAnglesSize) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const std::string_view moved = R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0.6,0,0.5]}}},
		{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[0.5]}}})";
	const str pivoted = str(moved) + R"(,{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"pivot"},"in":{"Pivot":{"value":[1.2,0,0]}}})";
	const str turned = pivoted + R"(,{"id":14,"def":"Block/SetAttribute","settings":{"Attribute":"angle"},"in":{"Angle":{"value":[0,180,0]}}})";
	const Vec3 front(0.0f, 0.0f, 3.0f);
	struct Case {
		const char* name;
		const str initialize;
		bool lit;
	};
	const Case cases[] = {{"Moved", str(moved), false}, {"Pivoted", pivoted, true}, {"Turned", turned, false}};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		Asset* imported = Import((str("Quad") + entry.name + ".sleffect").c_str(), entry.initialize, "");
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		bool drawn = false;
		const bool lit = Lit(imported, front, drawn);
		ASSERT_TRUE(drawn);
		EXPECT_EQ(lit, entry.lit) << (entry.lit ? "the pivot did not move the quad onto the middle" : "the quad covered a middle it was moved off");
		AssetDatabase::UnloadAsset(imported);
	}
}

namespace {

	void WriteFlipbookSheet(const std::filesystem::path& file) {
		constexpr u8 COLORS[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
		std::vector<u8> bytes(18, 0);
		bytes[2] = 2;
		bytes[12] = 8;
		bytes[14] = 8;
		bytes[16] = 32;
		bytes[17] = 0x28;
		for (u32 y = 0; y < 8; ++y) {
			for (u32 x = 0; x < 8; ++x) {
				const u8* color = COLORS[(y / 4) * 2 + x / 4];
				bytes.insert(bytes.end(), {color[2], color[1], color[0], 255});
			}
		}
		std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}

} // anonymous namespace

TEST_F(VisualEffectOrientFixture, FlipbookSamplesFrameOfMainTexture) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteFlipbookSheet(_root / "Sheet.tga");
	Asset* sheet = AssetDatabase::LoadAsset(Path("project://Sheet.tga"));
	ASSERT_NE(sheet, nullptr);
	const str texture = std::format(R"("Main Texture":{{"value":"{}"}})", sheet->assetId.ToString());
	struct Case {
		const char* name;
		f32 frame;
		str settings;
		str in;
		Vec3 color;
	};
	const str flipbook = R"("UV Mode":"Flipbook")";
	const str sheet2x2 = texture + R"(,"Flip Book Size":{"value":[2,2]})";
	const Case cases[] = {
		{"Frame0", 0.0f, flipbook, sheet2x2, {255.0f, 0.0f, 0.0f}},
		{"Frame1", 1.0f, flipbook, sheet2x2, {0.0f, 255.0f, 0.0f}},
		{"Frame2", 2.0f, flipbook, sheet2x2, {0.0f, 0.0f, 255.0f}},
		{"Frame3", 3.0f, flipbook, sheet2x2, {255.0f, 255.0f, 255.0f}},
		{"Blended", 0.5f, flipbook + R"(,"Flipbook Blend Frames":true)", sheet2x2, {128.0f, 128.0f, 0.0f}},
		{"ScaleAndBias", 0.0f, R"("UV Mode":"ScaleAndBias")", texture + R"(,"UV Scale":{"value":[0.5,0.5]},"UV Bias":{"value":[0.5,0]})", {255.0f, 255.0f, 255.0f}},
		{"NoMap", 0.0f, flipbook + R"(,"Use Base Color Map":"None")", sheet2x2, {255.0f, 255.0f, 255.0f}},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":20,"contexts":[
			{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[1]}}}}}}]}},
			{{"id":4,"def":"Context/Initialize","settings":{{"Capacity":4}},"flow":[[2,"In"]],"blocks":[
				{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[0,0,0.5]}}}}}},
				{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[1]}}}}}},
				{{"id":7,"def":"Block/SetAttribute","settings":{{"Attribute":"texIndex"}},"in":{{"TexIndex":{{"value":[{}]}}}}}}]}},
			{{"id":9,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"settings":{{{}}},"in":{{{}}},"blocks":[]}}]}})",
									  entry.frame, entry.settings, entry.in);
		const str name = str("Flipbook") + entry.name + ".sleffect";
		WriteProjectFile(name.c_str(), graph);
		Asset* imported = AssetDatabase::LoadAsset(Path("project://" + name));
		if (!imported) {
			AssetDatabase::UnloadAsset(sheet);
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		bool drawn = false;
		const Vec3 middle = Middle(imported, Vec3(0.0f, 0.0f, 3.0f), drawn);
		ASSERT_TRUE(drawn);
		const f32 got[3] = {middle.x, middle.y, middle.z};
		const f32 want[3] = {entry.color.x, entry.color.y, entry.color.z};
		for (u32 channel = 0; channel < 3; ++channel) {
			if (want[channel] == 128.0f) {
				EXPECT_TRUE(got[channel] > 60.0f && got[channel] < 230.0f) << "channel " << channel << ' ' << got[channel];
			} else {
				EXPECT_NEAR(got[channel], want[channel], 40.0f) << "channel " << channel;
			}
		}
		AssetDatabase::UnloadAsset(imported);
	}
	AssetDatabase::UnloadAsset(sheet);
}

TEST_F(VisualEffectOrientFixture, TexturePortColorsParticle) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteFlipbookSheet(_root / "Sheet.tga");
	Asset* sheet = AssetDatabase::LoadAsset(Path("project://Sheet.tga"));
	ASSERT_NE(sheet, nullptr);
	const str colored = R"({"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[20,"s"]}}})";
	struct Case {
		const char* name;
		const char* uv;
		bool inKernel;
		Vec3 color;
	};
	const Case cases[] = {
		{"Kernel", "[0.25,0.75]", true, {255.0f, 0.0f, 0.0f}},
		{"Output", "[0.25,0.25]", false, {0.0f, 0.0f, 255.0f}},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":30,
			"nodes":[{{"id":20,"def":"Sampling/SampleTexture2D","in":{{"Texture":{{"value":"{}"}},"UV":{{"value":{}}}}}}}],"contexts":[
			{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[1]}}}}}}]}},
			{{"id":4,"def":"Context/Initialize","settings":{{"Capacity":4}},"flow":[[2,"In"]],"blocks":[
				{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[0,0,0.5]}}}}}},
				{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[1]}}}}}}{}]}},
			{{"id":9,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"settings":{{"Use Base Color Map":"None"}},"blocks":[{}]}}]}})",
									  sheet->assetId.ToString(), entry.uv, entry.inKernel ? "," + colored : str(), entry.inKernel ? str() : colored);
		const str name = str("PortTexture") + entry.name + ".sleffect";
		WriteProjectFile(name.c_str(), graph);
		Asset* imported = AssetDatabase::LoadAsset(Path("project://" + name));
		if (!imported) {
			AssetDatabase::UnloadAsset(sheet);
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		bool drawn = false;
		const Vec3 middle = Middle(imported, Vec3(0.0f, 0.0f, 3.0f), drawn);
		ASSERT_TRUE(drawn);
		EXPECT_NEAR(middle.x, entry.color.x, 40.0f);
		EXPECT_NEAR(middle.y, entry.color.y, 40.0f);
		EXPECT_NEAR(middle.z, entry.color.z, 40.0f);
		AssetDatabase::UnloadAsset(imported);
	}
	AssetDatabase::UnloadAsset(sheet);
}

class VisualEffectOutputFixture : public VisualEffectOrientFixture {
protected:
	static constexpr f32 UNIT = 64.0f / (2.0f * 2.5f * 0.57735026918962576f);

	Asset* ImportDrawn(const char* name, u32 count, f32 size, std::string_view initialize, std::string_view outputs, bool spread = false) {
		const f32 half = 0.5f / UNIT;
		const str position = spread ? str(R"("Position":{"from":[22,"Out"]})") : std::format(R"("Position":{{"value":[{},{},0.5]}})", half, -half);
		const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":40,"nodes":[
			{{"id":20,"def":"Attribute/GetAttribute","settings":{{"Attribute":"spawnIndex"}}}},
			{{"id":21,"def":"Math/Multiply","in":{{"A":{{"from":[20,"spawnIndex"]}},"B":{{"value":[1.2,0,0,0]}}}}}},
			{{"id":23,"def":"Math/Add","in":{{"A":{{"from":[21,"Out"]}},"B":{{"value":[{},0,0,0]}}}}}},
			{{"id":22,"def":"Input/Vector3","in":{{"X":{{"from":[23,"Out"]}},"Y":{{"value":[{}]}},"Z":{{"value":[0.5]}}}}}}],"contexts":[
			{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[{}]}}}}}}]}},
			{{"id":4,"def":"Context/Initialize","settings":{{"Capacity":8}},"flow":[[2,"In"]],"blocks":[
				{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{{}}}}},
				{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[{}]}}}}}},
				{{"id":7,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"value":[0,1,0]}}}}}}{}]}},
			{}]}})",
									  half, -half, count, position, size, initialize, outputs);
		WriteProjectFile(name, graph);
		return AssetDatabase::LoadAsset(Path(str("project://") + name));
	}

	static str Output(u32 id, std::string_view def, std::string_view settings = "{}", std::string_view in = "{}", std::string_view blocks = "") {
		return std::format(R"({{"id":{},"def":"{}","flow":[[4,"In"]],"settings":{},"in":{},"blocks":[{}]}})", id, def, settings, in, blocks);
	}

	bool Render(Asset* imported, Vector<u8>& pixels, Vec2I& size, const std::function<void(ShadingStage&)>& dress = {}) {
		MaterialAsset anyMaterial;
		anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
		ShadingStage stage;
		if (!BuildStage(&anyMaterial, stage)) {
			return false;
		}
		stage.quad->SetActive(false);
		if (dress) {
			dress(stage);
		}
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
		static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();
		FrameStages::Run(FrameStage::LateUpdate, {});
		const bool drawn = RenderStage(stage, pixels, size);
		TearDownStage(stage);
		return drawn;
	}

	static u32 Lit(const Vector<u8>& pixels, u32 channel = 1) {
		u32 lit = 0;
		for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
			lit += pixels[i + channel] > 150 ? 1u : 0u;
		}
		return lit;
	}

	static bool LitAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y, u32 channel = 1) {
		return pixels[(static_cast<size_t>(y) * size.x + x) * 4 + channel] > 150;
	}

	static constexpr Vec3 FOG_COLOR{0.1f, 0.3f, 0.9f};
	static void InFog(ShadingStage& stage) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Environment", stage.scene);
		Handle<Environment> environment = object->AddComponent<Environment>();
		environment->SetSkyMode(SkyMode::None);
		environment->SetFog(true);
		environment->SetFogMode(FogMode::Linear);
		environment->SetFogColor(Color(FOG_COLOR.x, FOG_COLOR.y, FOG_COLOR.z, 1.0f));
		environment->SetFogStart(1.0f);
		environment->SetFogEnd(4.0f);
		SceneSystem::EndFrame();
	}

	u32 Footprint(Asset* imported) {
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		return imported && Render(imported, pixels, size) ? Lit(pixels) : 0u;
	}

	Vec3 OverRed(Asset* imported) {
		MaterialAsset red;
		red.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
		red.SetColor("baseColor", Color(1.0f, 0.0f, 0.0f, 1.0f));
		ShadingStage stage;
		if (!imported || !BuildStage(&red, stage)) {
			ADD_FAILURE() << "no stage";
			return Vec3{};
		}
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
		static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();
		FrameStages::Run(FrameStage::LateUpdate, {});
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		const bool drawn = RenderStage(stage, pixels, size);
		TearDownStage(stage);
		EXPECT_TRUE(drawn);
		return drawn ? CenterTexel(pixels, size) : Vec3{};
	}

	Asset* ImportAtAlpha(const char* name, f32 alpha) {
		return ImportDrawn(name, 1, 1.0f, std::format(R"(,{{"id":8,"def":"Block/SetAttribute","settings":{{"Attribute":"alpha"}},"in":{{"Alpha":{{"value":[{}]}}}}}})", alpha),
						   Output(9, "Context/OutputParticlePrimitive", R"({"Use Base Color Map":"None"})"));
	}
};

TEST_F(VisualEffectOutputFixture, PrimitiveIsTriangleQuadOrOctagon) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	Vector<u8> quad{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> triangle{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> octagon{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	Asset* quadAsset = ImportDrawn("Quad.sleffect", 1, 1.0f, "", Output(9, "Context/OutputParticlePrimitive"));
	if (!quadAsset) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* triangleAsset = ImportDrawn("Triangle.sleffect", 1, 1.0f, "", Output(9, "Context/OutputParticlePrimitive", R"({"Primitive Type":"Triangle"})"));
	Asset* octagonAsset = ImportDrawn("Octagon.sleffect", 1, 1.0f, "", Output(9, "Context/OutputParticlePrimitive", R"({"Primitive Type":"Octagon"})"));
	Asset* squareAsset = ImportDrawn("Square.sleffect", 1, 1.0f, "",
									 Output(9, "Context/OutputParticlePrimitive", R"({"Primitive Type":"Octagon"})", R"({"Crop Factor":{"value":[0]}})"));
	ASSERT_TRUE(triangleAsset && octagonAsset && squareAsset);
	ASSERT_TRUE(Render(quadAsset, quad, size));
	ASSERT_TRUE(Render(triangleAsset, triangle, size));
	ASSERT_TRUE(Render(octagonAsset, octagon, size));
	const f32 area = UNIT * UNIT;
	EXPECT_NEAR(static_cast<f32>(Lit(quad)), area, 2.0f * UNIT);
	EXPECT_NEAR(static_cast<f32>(Lit(triangle)), 0.4330127f * area, 1.5f * UNIT);
	EXPECT_NEAR(static_cast<f32>(Lit(octagon)), 0.7071068f * area, 4.0f * 0.3826834f * UNIT);
	EXPECT_EQ(Footprint(squareAsset), Lit(quad));

	EXPECT_TRUE(LitAt(triangle, size, 31, 22));
	EXPECT_FALSE(LitAt(triangle, size, 31, 41));
	EXPECT_TRUE(LitAt(quad, size, 31, 41));
	EXPECT_TRUE(LitAt(quad, size, 41, 41));
	EXPECT_FALSE(LitAt(octagon, size, 41, 41));
	EXPECT_TRUE(LitAt(octagon, size, 40, 32));

	for (Asset* imported : {quadAsset, triangleAsset, octagonAsset, squareAsset}) {
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectOutputFixture, APointIsOneTexelAParticle) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	Asset* one = ImportDrawn("Point.sleffect", 1, 3.0f, "", Output(9, "Context/OutputParticlePoint"));
	if (!one) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* two = ImportDrawn("Points.sleffect", 2, 3.0f, "", Output(9, "Context/OutputParticlePoint"), true);
	ASSERT_NE(two, nullptr);
	EXPECT_EQ(Footprint(one), 1u);
	EXPECT_EQ(Footprint(two), 2u);
	AssetDatabase::UnloadAsset(one);
	AssetDatabase::UnloadAsset(two);
}

TEST_F(VisualEffectOutputFixture, LineIsTexelWide) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const f32 half = 0.5f / UNIT;
	const str placed = std::format(R"(,{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{},{},0.5]}}}}}})",
								   half, -half);
	struct Case {
		const char* name;
		str initialize;
		const char* settings;
		bool column;
		f32 length;
	};
	const str target = placed + std::format(R"(,{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"targetPosition"}},"in":{{"TargetPosition":{{"value":[{},{},0.5]}}}}}})",
											half - 0.5f, -half);
	const Case cases[] = {
		{"Quad", placed, R"({"Blend Mode":"Opaque"})", true, 1.0f},
		{"Native", placed, R"({"Blend Mode":"Opaque","Use Native Lines":true})", true, 1.0f},
		{"Target", target, R"({"Blend Mode":"Opaque","Use Target Offset":false})", false, 0.5f},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		Asset* imported = ImportDrawn((str("Line") + entry.name + ".sleffect").c_str(), 1, 1.0f, entry.initialize, Output(9, "Context/OutputParticleLine", entry.settings));
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Render(imported, pixels, size));
		u32 lit = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				if (LitAt(pixels, size, x, y)) {
					++lit;
					EXPECT_TRUE(entry.column ? x == 31 && y <= 32 : y == 32 && x >= 31) << "texel (" << x << ", " << y;
				}
			}
		}
		EXPECT_NEAR(static_cast<f32>(lit), entry.length * UNIT, 2.0f);
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectOutputFixture, OutputFoggedLikeBlend) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const Vec3 green{0.0f, 1.0f, 0.0f};
	const Vec3 halfFogged = green * 0.5f + FOG_COLOR * 0.5f;
	const f32 half = 0.5f / UNIT;
	const str placed = std::format(R"(,{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{},{},0.5]}}}}}})",
								   half, -half);
	struct Case {
		const char* name;
		str initialize;
		str output;
		Vec2I texel;
		Vec3 expected;
	};
	const Case cases[] = {
		{"Alpha", "", Output(9, "Context/OutputParticlePrimitive"), {32, 32}, halfFogged},
		{"Additive", "", Output(9, "Context/OutputParticlePrimitive", R"({"Blend Mode":"Additive"})"), {32, 32}, green * 0.5f},
		{"Line", placed, Output(9, "Context/OutputParticleLine", R"({"Blend Mode":"Opaque"})"), {31, 30}, halfFogged},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		Asset* imported = ImportDrawn((str("Fogged") + entry.name + ".sleffect").c_str(), 1, 1.0f, entry.initialize, entry.output);
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Render(imported, pixels, size, InFog));
		const size_t at = (static_cast<size_t>(entry.texel.y) * size.x + entry.texel.x) * 4;
		const Vec3 texel(pixels[at], pixels[at + 1], pixels[at + 2]);
		const Vec3 expected = ExpectedTexel(entry.expected);
		EXPECT_NEAR(texel.x, expected.x, 1.5f);
		EXPECT_NEAR(texel.y, expected.y, 1.5f);
		EXPECT_NEAR(texel.z, expected.z, 1.5f);
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectOutputFixture, MeshOutputDrawsMeshParticle) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
	ASSERT_NE(quadMesh, nullptr);
	WriteFlipbookSheet(_root / "Sheet.tga");
	Asset* sheet = AssetDatabase::LoadAsset(Path("project://Sheet.tga"));
	ASSERT_NE(sheet, nullptr);
	const str texture = std::format(R"("Main Texture":{{"value":"{}"}})", sheet->assetId.ToString());
	const str mesh = std::format(R"("Mesh":{{"value":"{}"}})", quadMesh->assetId.ToString());

	const std::string_view white = R"(,{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"value":[1,1,1]}}})";
	Asset* planar = ImportDrawn("Planar.sleffect", 1, 1.0f, white, Output(9, "Context/OutputParticlePrimitive", "{}", "{" + texture + "}"));
	if (!planar) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* meshed = ImportDrawn("Meshed.sleffect", 1, 1.0f, white, Output(9, "Context/OutputParticleMesh", "{}", "{" + mesh + "," + texture + "}"));
	Asset* cube = ImportDrawn("Cube.sleffect", 1, 1.0f, "", Output(9, "Context/OutputParticleMesh", R"({"Use Base Color Map":"None"})"));
	Asset* one = ImportDrawn("CubeOne.sleffect", 1, 0.4f, "", Output(9, "Context/OutputParticleMesh", R"({"Use Base Color Map":"None"})"));
	Asset* two = ImportDrawn("CubeTwo.sleffect", 2, 0.4f, "", Output(9, "Context/OutputParticleMesh", R"({"Use Base Color Map":"None"})"), true);
	ASSERT_TRUE(meshed && cube && one && two);
	Vector<u8> planarPixels{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> meshedPixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(planar, planarPixels, size));
	ASSERT_TRUE(Render(meshed, meshedPixels, size));
	u32 planarCovered = 0;
	u32 meshedCovered = 0;
	u32 differing = 0;
	for (size_t i = 0; i + 3 < planarPixels.size(); i += 4) {
		const bool a = planarPixels[i] + planarPixels[i + 1] + planarPixels[i + 2] > 150;
		const bool b = meshedPixels[i] + meshedPixels[i + 1] + meshedPixels[i + 2] > 150;
		planarCovered += a ? 1u : 0u;
		meshedCovered += b ? 1u : 0u;
		differing += a && b && std::abs(planarPixels[i] - meshedPixels[i]) + std::abs(planarPixels[i + 2] - meshedPixels[i + 2]) > 120 ? 1u : 0u;
	}
	EXPECT_GT(planarCovered, 400u);
	EXPECT_NEAR(static_cast<f32>(meshedCovered), static_cast<f32>(planarCovered), static_cast<f32>(planarCovered) * 0.04f);
	EXPECT_LT(differing, planarCovered / 20);

	const f32 face = 1.25f * UNIT;
	EXPECT_NEAR(static_cast<f32>(Footprint(cube)), face * face, 2.0f * face);
	const f32 single = static_cast<f32>(Footprint(one));
	ASSERT_GT(single, 20.0f);
	const f32 pair = static_cast<f32>(Footprint(two)) / single;
	EXPECT_TRUE(pair > 1.8f && pair < 2.6f) << pair;

	for (Asset* imported : {planar, meshed, cube, one, two, sheet}) {
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectOutputFixture, OutputsDrawOwnCounts) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const str red = R"({"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"value":[1,0,0]}}})";
	const str aside = R"({"id":31,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[1.2,0,0.5]}}})";
	const str outputs = Output(9, "Context/OutputParticlePoint", "{}", "{}", red) + "," + Output(10, "Context/OutputParticlePrimitive", R"({"Use Base Color Map":"None"})", "{}", aside);
	Asset* imported = ImportDrawn("TwoOutputs.sleffect", 1, 1.0f, "", outputs);
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(imported, pixels, size));
	EXPECT_EQ(Lit(pixels, 0), 1u);
	EXPECT_GT(Lit(pixels, 1), 200u);
	EXPECT_FALSE(LitAt(pixels, size, 31, 32, 1));
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectOutputFixture, EachViewCompositesNearerParticleLast) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteProjectFile("Sorted.sleffect", R"({"_type":"EffectGraph","version":4,"nextId":40,"nodes":[
		{"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
		{"id":22,"def":"Input/Vector3","in":{"Z":{"from":[20,"spawnIndex"]}}},
		{"id":24,"def":"Input/Vector3","in":{"X":{"from":[20,"spawnIndex"]},"Y":{"from":[20,"spawnIndex"]},"Z":{"from":[20,"spawnIndex"]}}}],"contexts":[
		{"id":2,"def":"Context/Spawn","blocks":[{"id":3,"def":"Block/Burst","in":{"Count":{"value":[2]}}}]},
		{"id":4,"def":"Context/Initialize","settings":{"Capacity":8},"flow":[[2,"In"]],"blocks":[
			{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[22,"Out"]}}},
			{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[3]}}},
			{"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[24,"Out"]}}},
			{"id":8,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"value":[0.5]}}}]},
		{"id":9,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"settings":{"Use Base Color Map":"None"},"blocks":[]}]})");
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Sorted.sleffect"));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	MaterialAsset anyMaterial;
	anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&anyMaterial, stage));
	stage.quad->SetActive(false);
	const ViewTargetId behind = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(behind, INVALID_VIEW_TARGET);
	Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
	Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
	static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
	SceneSystem::EndFrame();
	FrameStages::Run(FrameStage::LateUpdate, {});
	ViewDesc fromBehind = QuadView();
	fromBehind.cameraPosition = Vec3(0.0f, 0.0f, -3.0f);
	fromBehind.view = Mat4::LookAt(fromBehind.cameraPosition, Vec3{});
	RenderSystem::SubmitView(stage.target, QuadView());
	RenderSystem::SubmitView(behind, fromBehind);
	RenderSystem::RenderAll();

	Vector<u8> front{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> back{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, front, size));
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(behind, back, size));
	const f32 whiteOverBlack = CenterTexel(front, size).x;
	const f32 blackOverWhite = CenterTexel(back, size).x;
	EXPECT_GT(blackOverWhite, 20.0f);
	EXPECT_GT(whiteOverBlack, blackOverWhite + 30.0f) << whiteOverBlack << " against " << blackOverWhite;

	RenderSystem::DestroyViewTarget(behind);
	TearDownStage(stage);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectOutputFixture, AlphaClippingDiscardsBelowItsThreshold) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const str alpha = R"(,{"id":8,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"value":[0.6]}}})";
	const auto clipped = [&](const char* name, f32 threshold) {
		return ImportDrawn(name, 1, 1.0f, alpha,
						   Output(9, "Context/OutputParticlePrimitive", R"({"Use Alpha Clipping":true,"Use Base Color Map":"None"})",
								  std::format(R"({{"Alpha Threshold":{{"value":[{}]}}}})", threshold)));
	};
	Asset* kept = clipped("Kept.sleffect", 0.5f);
	if (!kept) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* discarded = clipped("Discarded.sleffect", 0.7f);
	ASSERT_NE(discarded, nullptr);
	EXPECT_NEAR(static_cast<f32>(Footprint(kept)), UNIT * UNIT, 2.0f * UNIT);
	EXPECT_EQ(Footprint(discarded), 0u);
	AssetDatabase::UnloadAsset(kept);
	AssetDatabase::UnloadAsset(discarded);
}

TEST_F(VisualEffectOutputFixture, CameraFadeFadesByDepthAlongView) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const auto faded = [&](const char* name, std::string_view settings, std::string_view in) {
		const str block = std::format(R"({{"id":30,"def":"Block/CameraFade","settings":{},"in":{}}})", settings, in);
		return ImportDrawn(name, 1, 1.0f, "", Output(9, "Context/OutputParticlePrimitive", R"({"Use Base Color Map":"None"})", "{}", block));
	};
	Asset* whole = faded("Whole.sleffect", "{}", "{}");
	if (!whole) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	const std::string_view midway = R"({"Faded Distance":{"value":[2]},"Visible Distance":{"value":[3]}})";
	const std::string_view past = R"({"Faded Distance":{"value":[2.6]},"Visible Distance":{"value":[3]}})";
	Asset* half = faded("Half.sleffect", "{}", midway);
	Asset* reference = ImportAtAlpha("HalfAlpha.sleffect", 0.5f);
	Asset* culled = faded("Culled.sleffect", R"({"Fade Mode":"Color"})", past);
	Asset* kept = faded("Kept.sleffect", R"({"Fade Mode":"Color","Cull When Faded":false})", past);
	ASSERT_TRUE(half && reference && culled && kept);

	const Vec3 full = OverRed(whole);
	EXPECT_GT(full.y, 240.0f);
	EXPECT_LT(full.x, 10.0f);
	const Vec3 halved = OverRed(half);
	const Vec3 expected = OverRed(reference);
	EXPECT_NEAR(halved.x, expected.x, 3.0f);
	EXPECT_NEAR(halved.y, expected.y, 3.0f);
	EXPECT_GT(OverRed(culled).x, 240.0f);
	EXPECT_LT(OverRed(kept).x, 10.0f);
	for (Asset* imported : {whole, half, reference, culled, kept}) {
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectOutputFixture, SoftParticleFadesIntoSurfaceBehindIt) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const auto soft = [&](const char* name, f32 distance) {
		return ImportDrawn(name, 1, 1.0f, "",
						   Output(9, "Context/OutputParticlePrimitive", R"({"Use Soft Particle":true,"Use Base Color Map":"None"})",
								  std::format(R"({{"Soft Particle Fade Distance":{{"value":[{}]}}}})", distance)));
	};
	Asset* wide = soft("Wide.sleffect", 2.0f);
	if (!wide) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* narrow = soft("Narrow.sleffect", 0.1f);
	Asset* smoothed = ImportAtAlpha("Smoothed.sleffect", 0.15625f);
	Asset* linear = ImportAtAlpha("Linear.sleffect", 0.25f);
	ASSERT_TRUE(narrow && smoothed && linear);
	const Vec3 faded = OverRed(wide);
	const Vec3 expected = OverRed(smoothed);
	EXPECT_NEAR(faded.y, expected.y, 3.0f);
	EXPECT_NEAR(faded.x, expected.x, 3.0f);
	EXPECT_GT(OverRed(linear).y, expected.y + 10.0f);
	EXPECT_LT(OverRed(narrow).x, 10.0f);
	for (Asset* imported : {wide, narrow, smoothed, linear}) {
		AssetDatabase::UnloadAsset(imported);
	}
}

namespace {

	constexpr const char* SHADOW_TALLY_SHADER =
		"#define SHALLTEAR_CUSTOM_LIGHTING\n"
		"#pragma shalltear_template lit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
		"float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
		"\treturn float3(light.shadow, 0.0, 0.25);\n"
		"}\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = float3(0.0, 0.0, 0.0);\n"
		"\tsurface.reflectance = 0.0;\n"
		"}\n";

} // anonymous namespace

class VisualEffectLitFixture : public VisualEffectOutputFixture {
protected:
	u32 Shadowed(Asset* imported) {
		WriteProjectFile("Tally.hlsl", SHADOW_TALLY_SHADER);
		MaterialAsset tally;
		tally.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("project://Tally.hlsl"))));
		ShadingStage stage;
		if (!imported || !tally.shader.IsValid() || !BuildStage(&tally, stage)) {
			ADD_FAILURE() << "no stage";
			return 0;
		}
		stage.quad->SetLocalScale(Vec3(8.0f, 8.0f, 1.0f));
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
		static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();
		FrameStages::Run(FrameStage::LateUpdate, {});
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		const bool drawn = RenderStage(stage, pixels, size);
		TearDownStage(stage);
		EXPECT_TRUE(drawn);
		u32 count = 0;
		for (size_t i = 0; drawn && i + 3 < pixels.size(); i += 4) {
			count += pixels[i + 2] > 100 && pixels[i + 2] < 180 && pixels[i] < 128 ? 1u : 0u;
		}
		return count;
	}

	Asset* ImportAbove(const char* name, std::string_view initialize, std::string_view output) {
		return ImportDrawn(name, 1, 0.4f, str(R"(,{"id":15,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,0,1]}}})") + str(initialize),
						   output);
	}
};

TEST_F(VisualEffectLitFixture, LitParticleShadesLikeQuad) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const std::string_view facing = R"({"id":30,"def":"Block/Orient"})";
	Asset* lit = ImportDrawn("Lit.sleffect", 1, 1.0f, "", Output(9, "Context/OutputParticleLitPrimitive", R"({"Use Base Color Map":"None"})", "{}", facing));
	if (!lit) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* unlit = ImportDrawn("Unlit.sleffect", 1, 1.0f, "", Output(9, "Context/OutputParticlePrimitive", R"({"Use Base Color Map":"None"})", "{}", facing));
	ASSERT_NE(unlit, nullptr);

	MaterialAsset green;
	green.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"))));
	green.SetColor("baseColor", Color(0.0f, 1.0f, 0.0f, 1.0f));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&green, stage));
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	TearDownStage(stage);
	const Vec3 quad = CenterTexel(pixels, size);

	ASSERT_TRUE(Render(lit, pixels, size));
	const Vec3 particle = CenterTexel(pixels, size);
	EXPECT_GT(particle.y, 60.0f);
	EXPECT_NEAR(particle.x, quad.x, 4.0f);
	EXPECT_NEAR(particle.y, quad.y, 4.0f);
	EXPECT_NEAR(particle.z, quad.z, 4.0f);
	ASSERT_TRUE(Render(unlit, pixels, size));
	EXPECT_GT(std::abs(CenterTexel(pixels, size).y - quad.y), 10.0f);
	AssetDatabase::UnloadAsset(lit);
	AssetDatabase::UnloadAsset(unlit);
}

TEST_F(VisualEffectLitFixture, DoubleSidedShadesFront) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const std::string_view facing = R"({"id":30,"def":"Block/Orient"})";
	for (std::string_view primitive : {"Triangle", "Quad", "Octagon"}) {
		SCOPED_TRACE(primitive);
		const auto output = [&](bool both) {
			return Output(9, "Context/OutputParticleLitPrimitive",
						  std::format(R"({{"Use Base Color Map":"None","Primitive Type":"{}","Double Sided":{}}})", primitive, both ? "true" : "false"), "{}", facing);
		};
		Asset* single = ImportDrawn((str(primitive) + "Single.sleffect").c_str(), 1, 1.0f, "", output(false));
		if (!single) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Asset* both = ImportDrawn((str(primitive) + "Both.sleffect").c_str(), 1, 1.0f, "", output(true));
		ASSERT_NE(both, nullptr);
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Render(single, pixels, size));
		const Vec3 front = CenterTexel(pixels, size);
		ASSERT_GT(front.y, 60.0f);
		ASSERT_TRUE(Render(both, pixels, size));
		const Vec3 doubled = CenterTexel(pixels, size);
		EXPECT_NEAR(doubled.x, front.x, 4.0f);
		EXPECT_NEAR(doubled.y, front.y, 4.0f);
		EXPECT_NEAR(doubled.z, front.z, 4.0f);
		AssetDatabase::UnloadAsset(single);
		AssetDatabase::UnloadAsset(both);
	}
}

TEST_F(VisualEffectLitFixture, ParticleCastsShadowWhenToldTo) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const auto output = [](std::string_view settings, std::string_view blocks = "") {
		return Output(9, "Context/OutputParticlePrimitive", settings, "{}", blocks);
	};
	Asset* casting = ImportAbove("Casting.sleffect", "", output(R"({"Use Base Color Map":"None","Blend Mode":"Opaque","Cast Shadows":true})"));
	if (!casting) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* quiet = ImportAbove("Quiet.sleffect", "", output(R"({"Use Base Color Map":"None","Blend Mode":"Opaque"})"));
	const str faint = R"(,{"id":16,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"value":[0.3]}}})";
	const str dense = R"(,{"id":16,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"value":[0.8]}}})";
	Asset* faintCaster = ImportAbove("Faint.sleffect", faint, output(R"({"Use Base Color Map":"None","Cast Shadows":true})"));
	Asset* denseCaster = ImportAbove("Dense.sleffect", dense, output(R"({"Use Base Color Map":"None","Cast Shadows":true})"));
	const auto fade = [](bool affects) {
		return std::format(R"({{"id":30,"def":"Block/CameraFade","settings":{{"Affect Shadows":{}}},"in":{{"Faded Distance":{{"value":[3]}},"Visible Distance":{{"value":[4]}}}}}})",
						   affects ? "true" : "false");
	};
	Asset* fadedOnScreen = ImportAbove("FadedOnScreen.sleffect", "", output(R"({"Use Base Color Map":"None","Blend Mode":"Opaque","Cast Shadows":true})", fade(false)));
	Asset* fadedEverywhere = ImportAbove("FadedEverywhere.sleffect", "", output(R"({"Use Base Color Map":"None","Blend Mode":"Opaque","Cast Shadows":true})", fade(true)));
	ASSERT_TRUE(quiet && faintCaster && denseCaster && fadedOnScreen && fadedEverywhere);

	EXPECT_GT(Shadowed(casting), 30u);
	EXPECT_EQ(Shadowed(quiet), 0u);
	EXPECT_EQ(Shadowed(faintCaster), 0u);
	EXPECT_GT(Shadowed(denseCaster), 30u);
	EXPECT_EQ(Footprint(fadedOnScreen), 0u);
	EXPECT_GT(Shadowed(fadedOnScreen), 30u);
	EXPECT_EQ(Shadowed(fadedEverywhere), 0u);
	for (Asset* imported : {casting, quiet, faintCaster, denseCaster, fadedOnScreen, fadedEverywhere}) {
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectLitFixture, SoftParticleFadesIntoOpaque) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const f32 half = 0.5f / UNIT;
	const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":60,"contexts":[
		{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[1]}}}}}}]}},
		{{"id":4,"def":"Context/Initialize","settings":{{"Capacity":4}},"flow":[[2,"In"]],"blocks":[
			{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{0},{1},0.3]}}}}}},
			{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[1]}}}}}},
			{{"id":7,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"value":[1,0,0]}}}}}}]}},
		{{"id":9,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"settings":{{"Use Base Color Map":"None","Blend Mode":"Opaque"}},"blocks":[]}},
		{{"id":12,"def":"Context/Spawn","blocks":[{{"id":13,"def":"Block/Burst","in":{{"Count":{{"value":[1]}}}}}}]}},
		{{"id":14,"def":"Context/Initialize","settings":{{"Capacity":4}},"flow":[[12,"In"]],"blocks":[
			{{"id":15,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{0},{1},0.5]}}}}}},
			{{"id":16,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[1]}}}}}},
			{{"id":17,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"value":[0,1,0]}}}}}}]}},
		{{"id":19,"def":"Context/OutputParticlePrimitive","flow":[[14,"In"]],"settings":{{"Use Base Color Map":"None","Use Soft Particle":true}},
			"in":{{"Soft Particle Fade Distance":{{"value":[1]}}}},"blocks":[]}}]}})",
								  half, -half);
	WriteProjectFile("OverOpaque.sleffect", graph);
	Asset* both = AssetDatabase::LoadAsset(Path("project://OverOpaque.sleffect"));
	if (!both) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* fifth = ImportAtAlpha("Fifth.sleffect", 0.104f);
	Asset* halfAlpha = ImportAtAlpha("HalfAlpha.sleffect", 0.5f);
	ASSERT_TRUE(fifth && halfAlpha);
	const Vec3 faded = OverRed(both);
	const Vec3 expected = OverRed(fifth);
	EXPECT_NEAR(faded.y, expected.y, 4.0f);
	EXPECT_NEAR(faded.x, expected.x, 4.0f);
	EXPECT_GT(OverRed(halfAlpha).y, expected.y + 20.0f);
	for (Asset* imported : {both, fifth, halfAlpha}) {
		AssetDatabase::UnloadAsset(imported);
	}
}

namespace {

	str NodeShaderText(std::string_view settings, std::string_view properties, std::string_view nodes, std::string_view contexts) {
		return std::format(R"({{"_type":"NodeShader","version":1,"nextId":60,"settings":{{"supportEffects":true,{}}},"properties":[{}],"nodes":[{}],"contexts":[{}],"groups":[],"notes":[]}})",
						   settings, properties, nodes, contexts);
	}

	str ColorFrom(u32 node, std::string_view port) {
		return std::format(R"({{"id":5,"def":"Context/Fragment","blocks":[{{"id":6,"def":"Block/BaseColor","in":{{"Value":{{"from":[{},"{}"]}}}}}},{{"id":7,"def":"Block/Alpha"}}]}})", node,
						   port);
	}

	constexpr std::string_view FLAT = R"("target":"unlit","blend":"opaque","cull":"off")";

	const str TintGraph = NodeShaderText(FLAT, R"({"id":1,"name":"Tint","reference":"_Tint","type":"Color","default":[0,1,0,1]})",
										 R"({"id":10,"def":"Input/Property","settings":{"property":1}})", ColorFrom(10, "Out"));

} // anonymous namespace

class VisualEffectNodeShaderFixture : public VisualEffectLitFixture {
protected:
	Guid AddGraph(const char* name, const str& text) {
		WriteProjectFile(name, text);
		return AssetDatabase::EnsureAssetIdentity(Path(str("project://") + name));
	}

	static str Shaded(std::string_view def, const Guid& graph, std::string_view settings = "", std::string_view in = "{}", std::string_view blocks = "") {
		const str stored = graph.IsValid() ? std::format(R"("Node Shader":"{}"{}{})", graph.ToString(), settings.empty() ? "" : ",", settings) : str(settings);
		return Output(9, def, "{" + stored + "}", in, blocks);
	}

	static Vec3 Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		const size_t at = (static_cast<size_t>(y) * size.x + x) * 4;
		return Vec3(pixels[at], pixels[at + 1], pixels[at + 2]);
	}
};

TEST_F(VisualEffectNodeShaderFixture, PerParticlePropertyShades) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const Guid tint = AddGraph("Tint.slshader", TintGraph);
	ASSERT_TRUE(tint.IsValid());
	const f32 half = 0.5f / UNIT;
	const auto effect = [&](const char* name, std::string_view in) {
		const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":40,"nodes":[
			{{"id":20,"def":"Attribute/GetAttribute","settings":{{"Attribute":"spawnIndex"}}}},
			{{"id":21,"def":"Math/Multiply","in":{{"A":{{"from":[20,"spawnIndex"]}},"B":{{"value":[1.2,0,0,0]}}}}}},
			{{"id":23,"def":"Math/Add","in":{{"A":{{"from":[21,"Out"]}},"B":{{"value":[{0},0,0,0]}}}}}},
			{{"id":22,"def":"Input/Vector3","in":{{"X":{{"from":[23,"Out"]}},"Y":{{"value":[{1}]}},"Z":{{"value":[0.5]}}}}}},
			{{"id":24,"def":"Math/Subtract","in":{{"A":{{"value":[1,0,0,0]}},"B":{{"from":[20,"spawnIndex"]}}}}}},
			{{"id":25,"def":"Input/Vector3","in":{{"X":{{"from":[20,"spawnIndex"]}},"Y":{{"value":[0]}},"Z":{{"from":[24,"Out"]}}}}}},
			{{"id":26,"def":"Attribute/GetAttribute","settings":{{"Attribute":"color"}}}}],"contexts":[
			{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[2]}}}}}}]}},
			{{"id":4,"def":"Context/Initialize","settings":{{"Capacity":8}},"flow":[[2,"In"]],"blocks":[
				{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"from":[22,"Out"]}}}}}},
				{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[1]}}}}}},
				{{"id":7,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"from":[25,"Out"]}}}}}}]}},
			{2}]}})",
									  half, -half, Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, tint, "", in));
		WriteProjectFile(name, graph);
		return AssetDatabase::LoadAsset(Path(str("project://") + name));
	};
	Asset* fed = effect("Fed.sleffect", R"({"_Tint":{"from":[26,"color"]}})");
	if (!fed) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* unfed = effect("Unfed.sleffect", "{}");
	ASSERT_NE(unfed, nullptr);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(fed, pixels, size));
	const Vec3 first = Texel(pixels, size, 32, 32);
	const Vec3 second = Texel(pixels, size, 8, 32);
	EXPECT_GT(first.z, 150.0f);
	EXPECT_LT(first.x, 40.0f);
	EXPECT_LT(first.y, 40.0f);
	EXPECT_GT(second.x, 150.0f);
	EXPECT_LT(second.z, 40.0f);
	EXPECT_LT(second.y, 40.0f);

	ASSERT_TRUE(Render(unfed, pixels, size));
	for (i32 x : {32, 8}) {
		const Vec3 texel = Texel(pixels, size, x, 32);
		EXPECT_GT(texel.y, 150.0f) << x;
		EXPECT_LT(texel.x + texel.z, 40.0f) << x;
	}
	AssetDatabase::UnloadAsset(fed);
	AssetDatabase::UnloadAsset(unfed);
}

TEST_F(VisualEffectNodeShaderFixture, NewOutputShadesBothFaces) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const std::string_view facing = R"({"id":30,"def":"Block/Orient"})";
	Asset* turned = ImportDrawn("Default.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, Guid{}, "", "{}", facing));
	if (!turned) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* behind = ImportDrawn("DefaultBehind.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, Guid{}));
	Asset* cube = ImportDrawn("DefaultMesh.sleffect", 1, 0.5f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH, Guid{}));
	ASSERT_TRUE(behind && cube);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (Asset* imported : {turned, behind, cube}) {
		ASSERT_TRUE(Render(imported, pixels, size));
		const Vec3 middle = CenterTexel(pixels, size);
		EXPECT_GT(middle.x, 200.0f);
		EXPECT_GT(middle.y, 200.0f);
		EXPECT_GT(middle.z, 200.0f);
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectNodeShaderFixture, BackCullingDrawsFront) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const Guid front = AddGraph("Front.slshader", NodeShaderText(R"("target":"unlit","blend":"opaque","cull":"back")", "", "",
																 R"({"id":5,"def":"Context/Fragment","blocks":[{"id":6,"def":"Block/BaseColor","in":{"Value":{"value":[1,1,1]}}}]})"));
	const std::string_view facing = R"({"id":30,"def":"Block/Orient"})";
	for (std::string_view primitive : {"Triangle", "Quad", "Octagon"}) {
		SCOPED_TRACE(primitive);
		const str type = std::format(R"("Primitive Type":"{}")", primitive);
		Asset* turned = ImportDrawn((str(primitive) + "Turned.sleffect").c_str(), 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, front, type, "{}", facing));
		if (!turned) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Asset* behind = ImportDrawn((str(primitive) + "Behind.sleffect").c_str(), 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, front, type));
		ASSERT_NE(behind, nullptr);
		EXPECT_GT(Footprint(turned), 50u);
		EXPECT_EQ(Footprint(behind), 0u);
		AssetDatabase::UnloadAsset(turned);
		AssetDatabase::UnloadAsset(behind);
	}
}

TEST_F(VisualEffectNodeShaderFixture, LitGraphShadesLikeQuad) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const str stack = R"({"id":5,"def":"Context/Fragment","blocks":[
		{"id":6,"def":"Block/BaseColor","in":{"Value":{"value":[0,1,0]}}},
		{"id":7,"def":"Block/Roughness","in":{"Value":{"value":[0.5]}}},
		{"id":8,"def":"Block/Metallic"},{"id":9,"def":"Block/Alpha"}]})";
	const Guid lit = AddGraph("Lit.slshader", NodeShaderText(R"("target":"lit","blend":"opaque","cull":"off")", "", "", stack));
	const Guid unlit = AddGraph("Unlit.slshader", NodeShaderText(FLAT, "", "", stack));
	const std::string_view facing = R"({"id":30,"def":"Block/Orient"})";
	Asset* litParticle = ImportDrawn("LitGraph.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, lit, "", "{}", facing));
	if (!litParticle) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* unlitParticle = ImportDrawn("UnlitGraph.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, unlit, "", "{}", facing));
	ASSERT_NE(unlitParticle, nullptr);

	MaterialAsset green;
	green.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"))));
	green.SetColor("baseColor", Color(0.0f, 1.0f, 0.0f, 1.0f));
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(&green, stage));
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	TearDownStage(stage);
	const Vec3 quad = CenterTexel(pixels, size);

	ASSERT_TRUE(Render(litParticle, pixels, size));
	const Vec3 particle = CenterTexel(pixels, size);
	EXPECT_GT(particle.y, 60.0f);
	EXPECT_NEAR(particle.x, quad.x, 4.0f);
	EXPECT_NEAR(particle.y, quad.y, 4.0f);
	EXPECT_NEAR(particle.z, quad.z, 4.0f);
	ASSERT_TRUE(Render(unlitParticle, pixels, size));
	EXPECT_GT(std::abs(CenterTexel(pixels, size).y - quad.y), 10.0f);
	AssetDatabase::UnloadAsset(litParticle);
	AssetDatabase::UnloadAsset(unlitParticle);
}

TEST_F(VisualEffectNodeShaderFixture, NodeShaderOutputFogged) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const str property = R"({"id":1,"name":"Tint","reference":"_Tint","type":"Color","default":[0,1,0,1]})";
	const str node = R"({"id":10,"def":"Input/Property","settings":{"property":1}})";
	const Guid opaque = AddGraph("Tint.slshader", TintGraph);
	const Guid additive = AddGraph("Added.slshader", NodeShaderText(R"("target":"unlit","blend":"additive","cull":"off")", property, node, ColorFrom(10, "Out")));
	ASSERT_TRUE(opaque.IsValid() && additive.IsValid());
	const Vec3 green{0.0f, 1.0f, 0.0f};
	struct Case {
		const char* name;
		Guid graph;
		Vec3 expected;
	};
	const Case cases[] = {
		{"Opaque", opaque, green * 0.5f + FOG_COLOR * 0.5f},
		{"Additive", additive, green * 0.5f},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		Asset* imported = ImportDrawn((str("FoggedGraph") + entry.name + ".sleffect").c_str(), 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, entry.graph));
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Render(imported, pixels, size, InFog));
		const Vec3 texel = CenterTexel(pixels, size);
		const Vec3 expected = ExpectedTexel(entry.expected);
		EXPECT_NEAR(texel.x, expected.x, 1.5f);
		EXPECT_NEAR(texel.y, expected.y, 1.5f);
		EXPECT_NEAR(texel.z, expected.z, 1.5f);
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectNodeShaderFixture, TexturePropertyUpright) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteFlipbookSheet(_root / "Sheet.tga");
	Asset* sheet = AssetDatabase::LoadAsset(Path("project://Sheet.tga"));
	ASSERT_NE(sheet, nullptr);
	const Guid sampled = AddGraph("Sampled.slshader", NodeShaderText(FLAT, R"({"id":1,"name":"Sheet","reference":"_Sheet","type":"Texture2D"})",
																	 R"({"id":10,"def":"Input/Property","settings":{"property":1}},
		{"id":11,"def":"Input/SampleTexture2D","in":{"Texture":{"from":[10,"Out"]}}})",
																	 ColorFrom(11, "RGBA")));
	const str held = std::format(R"({{"_Sheet":{{"value":"{}"}}}})", sheet->assetId.ToString());
	Asset* graph = ImportDrawn("SheetGraph.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, sampled, "", held));
	if (!graph) {
		AssetDatabase::UnloadAsset(sheet);
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* empty = ImportDrawn("EmptyGraph.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, sampled));
	const str white = R"(,{"id":8,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"value":[1,1,1]}}})";
	Asset* reference = ImportDrawn("SheetMain.sleffect", 1, 1.0f, white,
								   Output(9, "Context/OutputParticlePrimitive", R"({"Blend Mode":"Opaque"})",
										  std::format(R"({{"Main Texture":{{"value":"{}"}}}})", sheet->assetId.ToString())));
	ASSERT_TRUE(empty && reference);

	Vector<u8> graphPixels{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> referencePixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(graph, graphPixels, size));
	ASSERT_TRUE(Render(reference, referencePixels, size));
	for (const Vec2I at : {Vec2I{27, 27}, Vec2I{37, 27}, Vec2I{27, 37}, Vec2I{37, 37}}) {
		const Vec3 got = Texel(graphPixels, size, at.x, at.y);
		const Vec3 want = Texel(referencePixels, size, at.x, at.y);
		EXPECT_NEAR(got.x, want.x, 40.0f) << at.x << ", " << at.y;
		EXPECT_NEAR(got.y, want.y, 40.0f) << at.x << ", " << at.y;
		EXPECT_NEAR(got.z, want.z, 40.0f) << at.x << ", " << at.y;
	}
	const Vec3 upperLeft = Texel(graphPixels, size, 27, 27);
	const Vec3 upperRight = Texel(graphPixels, size, 37, 27);
	const Vec3 lowerLeft = Texel(graphPixels, size, 27, 37);
	const Vec3 lowerRight = Texel(graphPixels, size, 37, 37);
	EXPECT_GT(upperLeft.y, 200.0f);
	EXPECT_GT(upperRight.x, 200.0f);
	EXPECT_LT(upperRight.y + upperRight.z, 60.0f);
	EXPECT_GT(lowerLeft.x + lowerLeft.y + lowerLeft.z, 600.0f);
	EXPECT_GT(lowerRight.z, 200.0f);
	EXPECT_LT(lowerRight.x + lowerRight.y, 60.0f);
	ASSERT_TRUE(Render(empty, graphPixels, size));
	const Vec3 unset = CenterTexel(graphPixels, size);
	EXPECT_GT(unset.x + unset.y + unset.z, 600.0f);
	for (Asset* imported : {graph, empty, reference}) {
		AssetDatabase::UnloadAsset(imported);
	}
	AssetDatabase::UnloadAsset(sheet);
}

TEST_F(VisualEffectNodeShaderFixture, KeywordIsVariantOutputsSettingChose) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const Guid keyed = AddGraph("Keyed.slshader", NodeShaderText(FLAT, R"({"id":1,"name":"Use Red","reference":"_USE_RED","type":"BooleanKeyword","default":[0]})",
																 R"({"id":11,"def":"Input/Vector3","in":{"X":{"value":[1]}}},
		{"id":12,"def":"Input/Vector3","in":{"Y":{"value":[1]}}},
		{"id":10,"def":"Utility/Keyword","settings":{"property":1},"in":{"On":{"from":[11,"Out"]},"Off":{"from":[12,"Out"]}}})",
																 ColorFrom(10, "Out")));
	Asset* red = ImportDrawn("Red.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, keyed, R"("Use Red":true)"));
	if (!red) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* green = ImportDrawn("Green.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, keyed));
	ASSERT_NE(green, nullptr);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(red, pixels, size));
	EXPECT_GT(CenterTexel(pixels, size).x, 150.0f);
	EXPECT_LT(CenterTexel(pixels, size).y, 40.0f);
	ASSERT_TRUE(Render(green, pixels, size));
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);
	EXPECT_LT(CenterTexel(pixels, size).x, 40.0f);
	AssetDatabase::UnloadAsset(red);
	AssetDatabase::UnloadAsset(green);
}

TEST_F(VisualEffectNodeShaderFixture, NoiseSharesOneHelper) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const Guid noisy = AddGraph("Noisy.slshader", NodeShaderText(FLAT, R"({"id":1,"name":"Amount","reference":"_Amount","type":"Float","default":[1]})",
																 R"({"id":10,"def":"Input/Property","settings":{"property":1}},
		{"id":11,"def":"Procedural/GradientNoise"},
		{"id":12,"def":"Math/Add","in":{"A":{"from":[11,"Out"]},"B":{"from":[10,"Out"]}}})",
																 ColorFrom(12, "Out")));
	Asset* plain = ImportDrawn("Plain.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, noisy));
	if (!plain) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	AssetDatabase::UnloadAsset(plain);
	const f32 half = 0.5f / UNIT;
	const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":40,"nodes":[
		{{"id":20,"def":"Input/Vector2","in":{{"X":{{"value":[0.3]}},"Y":{{"value":[0.7]}}}}}},
		{{"id":21,"def":"Procedural/GradientNoise","in":{{"UV":{{"from":[20,"Out"]}}}}}}],"contexts":[
		{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[1]}}}}}}]}},
		{{"id":4,"def":"Context/Initialize","settings":{{"Capacity":4}},"flow":[[2,"In"]],"blocks":[
			{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{},{},0.5]}}}}}},
			{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[1]}}}}}}]}},
		{}]}})",
								  half, -half, Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, noisy, "", R"({"_Amount":{"from":[21,"Out"]}})"));
	WriteProjectFile("Noisy.sleffect", graph);
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Noisy.sleffect"));
	ASSERT_NE(imported, nullptr);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(imported, pixels, size));
	const Vec3 middle = CenterTexel(pixels, size);
	EXPECT_GT(middle.x + middle.y + middle.z, 30.0f);
	AssetDatabase::UnloadAsset(imported);
}

TEST_F(VisualEffectNodeShaderFixture, GraphsVertexStackMovesElementAndShadow) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const str fragment = R"({"id":5,"def":"Context/Fragment","blocks":[{"id":6,"def":"Block/BaseColor","in":{"Value":{"value":[0,1,0]}}},{"id":7,"def":"Block/Alpha"}]})";
	const Guid still = AddGraph("Still.slshader", NodeShaderText(FLAT, "", "", fragment));
	const Guid lifted = AddGraph("Lifted.slshader", NodeShaderText(FLAT, "", R"({"id":10,"def":"Input/Position","settings":{"Space":"Object"}},
		{"id":11,"def":"Math/Add","in":{"A":{"from":[10,"Out"]},"B":{"value":[0,0.5,0]}}})",
																   str(R"({"id":2,"def":"Context/Vertex","blocks":[{"id":3,"def":"Block/Position","in":{"Value":{"from":[11,"Out"]}}}]},)") + fragment));
	Asset* stillParticle = ImportDrawn("Still.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, still));
	if (!stillParticle) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Asset* liftedParticle = ImportDrawn("Lifted.sleffect", 1, 1.0f, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, lifted));
	ASSERT_NE(liftedParticle, nullptr);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stillParticle, pixels, size));
	EXPECT_TRUE(LitAt(pixels, size, 32, 38));
	EXPECT_FALSE(LitAt(pixels, size, 32, 14));
	ASSERT_TRUE(Render(liftedParticle, pixels, size));
	EXPECT_FALSE(LitAt(pixels, size, 32, 38));
	EXPECT_TRUE(LitAt(pixels, size, 32, 14));
	AssetDatabase::UnloadAsset(stillParticle);
	AssetDatabase::UnloadAsset(liftedParticle);

	const Guid far = AddGraph("Far.slshader", NodeShaderText(FLAT, "", R"({"id":10,"def":"Input/Position","settings":{"Space":"Object"}},
		{"id":11,"def":"Math/Add","in":{"A":{"from":[10,"Out"]},"B":{"value":[0,50,0]}}})",
															 str(R"({"id":2,"def":"Context/Vertex","blocks":[{"id":3,"def":"Block/Position","in":{"Value":{"from":[11,"Out"]}}}]},)") + fragment));
	const auto caster = [&](const char* name, const Guid& graph) {
		return ImportAbove(name, "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, graph, R"("Cast Shadows":true)"));
	};
	Asset* stillCaster = caster("StillCaster.sleffect", still);
	Asset* farCaster = caster("FarCaster.sleffect", far);
	Asset* quiet = ImportAbove("QuietGraph.sleffect", "", Shaded(GRAPH_EFFECT_OUTPUT_NODE_SHADER, still));
	ASSERT_TRUE(stillCaster && farCaster && quiet);
	EXPECT_GT(Shadowed(stillCaster), 30u);
	EXPECT_EQ(Shadowed(quiet), 0u);
	EXPECT_EQ(Shadowed(farCaster), 0u);
	for (Asset* imported : {stillCaster, farCaster, quiet}) {
		AssetDatabase::UnloadAsset(imported);
	}
}

namespace {

	void WriteHalvesSheet(const std::filesystem::path& file) {
		std::vector<u8> bytes(18, 0);
		bytes[2] = 2;
		bytes[12] = 8;
		bytes[14] = 1;
		bytes[16] = 32;
		bytes[17] = 0x28;
		for (u32 x = 0; x < 8; ++x) {
			const u8 value = x < 4 ? 0 : 255;
			bytes.insert(bytes.end(), {value, value, value, 255});
		}
		std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}

} // anonymous namespace

class VisualEffectStripFixture : public VisualEffectNodeShaderFixture {
protected:
	Asset* ImportStrip(const char* name, std::string_view lifetime, std::string_view output) {
		const str dies = lifetime.empty() ? str()
										  : std::format(R"(,{},{{"id":68,"def":"Math/Multiply","in":{{"A":{{"from":[60,"Out"]}},"B":{{"value":[10,0,0,0]}}}}}},
			{{"id":69,"def":"Math/Add","in":{{"A":{{"from":[68,"Out"]}},"B":{{"value":[0.05,0,0,0]}}}}}})",
														lifetime);
		const str lives = lifetime.empty() ? str() : str(R"(,{"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"from":[69,"Out"]}}})");
		const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[
			{{"id":20,"def":"Attribute/GetAttribute","settings":{{"Attribute":"particleIndexInStrip"}}}},
			{{"id":21,"def":"Math/Multiply","in":{{"A":{{"from":[20,"particleIndexInStrip"]}},"B":{{"value":[0.4,0,0,0]}}}}}},
			{{"id":22,"def":"Math/Subtract","in":{{"A":{{"from":[21,"Out"]}},"B":{{"value":[0.8,0,0,0]}}}}}},
			{{"id":23,"def":"Input/Vector3","in":{{"X":{{"from":[22,"Out"]}},"Z":{{"value":[0.5]}}}}}}{}],"contexts":[
			{{"id":2,"def":"Context/Spawn","blocks":[{{"id":3,"def":"Block/Burst","in":{{"Count":{{"value":[5]}}}}}}]}},
			{{"id":4,"def":"Context/Initialize","settings":{{"Data Type":"ParticleStrip","Strip Capacity":1,"Particle Per Strip Count":8}},"flow":[[2,"In"]],
				"blocks":[
				{{"id":5,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"from":[23,"Out"]}}}}}},
				{{"id":6,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[0.3]}}}}}},
				{{"id":7,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"value":[0,1,0]}}}}}}{}]}},
			{{"id":8,"def":"Context/Update","flow":[[4,"In"]],"blocks":[]}},
			{}]}})",
									  dies, lives, output);
		WriteProjectFile(name, graph);
		return AssetDatabase::LoadAsset(Path(str("project://") + name));
	}

	static str StripOutput(std::string_view def, std::string_view settings = "{}", std::string_view in = "{}") {
		return std::format(R"({{"id":9,"def":"{}","flow":[[8,"In"]],"settings":{},"in":{},
			"blocks":[{{"id":11,"def":"Block/Orient","settings":{{"Mode":"FaceCameraPosition"}}}}]}})",
						   def, settings, in);
	}

	bool Play(Asset* imported, u32 frames, Vector<u8>& pixels, Vec2I& size) {
		MaterialAsset anyMaterial;
		anyMaterial.shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"))));
		ShadingStage stage;
		if (!BuildStage(&anyMaterial, stage)) {
			return false;
		}
		stage.quad->SetActive(false);
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", stage.scene);
		Handle<Component> component = object->AddComponent(TypeRegistry::FindType(StringHash("VisualEffect")));
		static_cast<VisualEffect*>(component.Get())->asset = Handle<VisualEffectAsset>(static_cast<VisualEffectAsset*>(imported));
		SceneSystem::EndFrame();
		for (u32 frame = 0; frame < frames; ++frame) {
			Time::BeginFrame(0.1);
			FrameStages::Run(FrameStage::LateUpdate, {});
			RenderSystem::SubmitView(stage.target, QuadView());
			RenderSystem::RenderAll();
		}
		Time::ResetFrameClock();
		const bool drawn = RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
		TearDownStage(stage);
		return drawn;
	}

	static i32 Column(f32 x) { return static_cast<i32>(std::floor(32.0f - x * UNIT)); }
	static i32 Row(f32 y) { return static_cast<i32>(std::floor(32.0f - y * UNIT)); }
	static bool LitAtWorld(const Vector<u8>& pixels, Vec2I size, f32 x, f32 y = 0.0f, u32 channel = 1) {
		return LitAt(pixels, size, Column(x), Row(y), channel);
	}
};

TEST_F(VisualEffectStripFixture, StripSurvivesDeadParticle) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	struct Case {
		const char* name;
		std::string_view lifetime;
		bool head;
	};
	const Case cases[] = {
		{"Whole", "", true},
		{"Middle",
		 R"({"id":61,"def":"Math/Subtract","in":{"A":{"from":[20,"particleIndexInStrip"]},"B":{"value":[2,0,0,0]}}},
			{"id":62,"def":"Math/Sign","in":{"In":{"from":[61,"Out"]}}},
			{"id":60,"def":"Math/Absolute","in":{"In":{"from":[62,"Out"]}}})",
		 true},
		{"Head", R"({"id":60,"def":"Math/Step","in":{"Edge":{"value":[1,0,0,0]},"In":{"from":[20,"particleIndexInStrip"]}}})", false},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		Asset* imported = ImportStrip((str("Strip") + entry.name + ".sleffect").c_str(), entry.lifetime, StripOutput(GRAPH_EFFECT_OUTPUT_STRIP, R"({"Use Base Color Map":"None"})"));
		if (!imported) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Play(imported, 3, pixels, size));
		EXPECT_EQ(LitAtWorld(pixels, size, -0.6f), entry.head) << (entry.head ? "the strip's first segment is dark" : "the strip kept its dead head");
		for (const f32 x : {-0.2f, 0.0f, 0.2f, 0.6f}) {
			EXPECT_TRUE(LitAtWorld(pixels, size, x)) << x;
		}
		EXPECT_FALSE(LitAtWorld(pixels, size, 1.0f));
		EXPECT_FALSE(LitAtWorld(pixels, size, -1.0f));
		EXPECT_FALSE(LitAtWorld(pixels, size, 0.0f, 0.4f));
		AssetDatabase::UnloadAsset(imported);
	}
}

TEST_F(VisualEffectStripFixture, StripUVFollowsTiling) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	WriteHalvesSheet(_root / "Halves.tga");
	Asset* sheet = AssetDatabase::LoadAsset(Path("project://Halves.tga"));
	ASSERT_NE(sheet, nullptr);
	const str texture = std::format(R"("Main Texture":{{"value":"{}"}})", sheet->assetId.ToString());
	struct Sample {
		f32 x;
		bool lit;
	};
	struct Case {
		const char* name;
		const char* settings;
		str in;
		Vector<Sample> samples;
	};
	const Case cases[] = {
		{"Stretch", "{}", "{" + texture + "}", {{{-0.6f, false}, {-0.2f, false}, {0.2f, true}, {0.6f, true}}, GetMemoryResource(MemTag::Graphics)}},
		{"Repeat", R"({"Tiling Mode":"RepeatPerSegment"})", "{" + texture + "}", {{{-0.75f, false}, {-0.45f, true}, {0.05f, false}, {0.35f, true}}, GetMemoryResource(MemTag::Graphics)}},
		{"Custom", R"({"Tiling Mode":"Custom"})", "{" + texture + R"(,"Tex Coord":{"value":[0.9]})" + "}", {{{-0.6f, true}, {0.6f, true}}, GetMemoryResource(MemTag::Graphics)}},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		Asset* imported = ImportStrip((str("StripUV") + entry.name + ".sleffect").c_str(), "", StripOutput(GRAPH_EFFECT_OUTPUT_STRIP, entry.settings, entry.in));
		if (!imported) {
			AssetDatabase::UnloadAsset(sheet);
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Play(imported, 2, pixels, size));
		for (const Sample& sample : entry.samples) {
			EXPECT_EQ(LitAtWorld(pixels, size, sample.x), sample.lit) << "at x = " << sample.x;
		}
		AssetDatabase::UnloadAsset(imported);
	}

	Asset* swapped = ImportStrip("StripUVSwapped.sleffect", "", StripOutput(GRAPH_EFFECT_OUTPUT_STRIP, R"({"Swap UV":true})", "{" + texture + "}"));
	ASSERT_NE(swapped, nullptr);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Play(swapped, 2, pixels, size));
	EXPECT_NE(LitAtWorld(pixels, size, -0.6f, 0.1f), LitAtWorld(pixels, size, -0.6f, -0.1f));
	EXPECT_NE(LitAtWorld(pixels, size, 0.6f, 0.1f), LitAtWorld(pixels, size, 0.6f, -0.1f));
	AssetDatabase::UnloadAsset(swapped);
	AssetDatabase::UnloadAsset(sheet);
}

TEST_F(VisualEffectStripFixture, NodeShaderStripShadesSegmentsWithGraph) {
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device";
	}
	const std::string_view middle = R"({"id":61,"def":"Math/Subtract","in":{"A":{"from":[20,"particleIndexInStrip"]},"B":{"value":[2,0,0,0]}}},
		{"id":62,"def":"Math/Sign","in":{"In":{"from":[61,"Out"]}}},
		{"id":60,"def":"Math/Absolute","in":{"In":{"from":[62,"Out"]}}})";
	Asset* imported = ImportStrip("StripGraph.sleffect", middle, StripOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP));
	if (!imported) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Play(imported, 3, pixels, size));
	for (const f32 x : {-0.6f, -0.2f, 0.0f, 0.2f, 0.6f}) {
		EXPECT_TRUE(LitAtWorld(pixels, size, x, 0.0f, 0)) << x;
		EXPECT_TRUE(LitAtWorld(pixels, size, x, 0.0f, 2)) << x;
	}
	EXPECT_FALSE(LitAtWorld(pixels, size, 1.0f, 0.0f, 0));
	AssetDatabase::UnloadAsset(imported);
}

#endif
