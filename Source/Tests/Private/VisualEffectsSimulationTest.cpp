#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Window.h"

#include <algorithm>
#include <filesystem>

#ifndef SHALLTEAR_SHARED
#include "ParticleKernels.h"
#include "ParticleSimulation.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(VisualEffects)
#endif

namespace {

	constexpr const char* PACKAGE_TARGET = "VisualEffects";

	std::filesystem::path EffectsRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	class VisualEffectsDisableCycleFixture : public ::testing::Test {
	protected:
#ifdef SHALLTEAR_SHARED
		ModuleHandle _module;
#else
		char _token = 0;
#endif

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			ModuleSystem::Initialize();
		}

		void TearDown() override {
#ifndef SHALLTEAR_SHARED
			VisualEffectsImpl::ShutdownSimulation();
#endif
			ModuleSystem::Terminate();
		}

		bool StandUp() {
#ifdef SHALLTEAR_SHARED
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			return ModuleSystem::IsLoaded(_module);
#else
			SLT_CALL_LINKED_PACKAGE(VisualEffects, &_token);
			return true;
#endif
		}

		const void* OwnerToken() const {
#ifdef SHALLTEAR_SHARED
			return ModuleSystem::GetNativeHandle(_module);
#else
			return &_token;
#endif
		}
	};

} // namespace

TEST_F(VisualEffectsDisableCycleFixture, RegistersPassAndStage) {
	ASSERT_TRUE(StandUp());
	const void* owner = OwnerToken();
	ASSERT_NE(owner, nullptr);
	EXPECT_EQ(FrameStages::UnregisterOwnedBy(owner), 1u);
	EXPECT_EQ(FramePasses::UnregisterOwnedBy(owner), 1u);
}

#ifdef SHALLTEAR_SHARED
TEST_F(VisualEffectsDisableCycleFixture, TheFramePassLeavesWithTheImage) {
	ASSERT_TRUE(StandUp());
	const void* owner = OwnerToken();
	ModuleSystem::Unload(_module);
	EXPECT_FALSE(ModuleSystem::IsLoaded(_module));
	EXPECT_EQ(FramePasses::UnregisterOwnedBy(owner), 0u);
}
#endif

TEST(VisualEffectsKernelTest, CommittedSidecarsCarryGuidsPackageResolvesBy) {
	struct Expected {
		const char* path;
		const char* guid;
	};
	const Expected kernels[] = {
		{"EngineAssets/Shaders/VisualEffects/ParticleReset.hlsl.meta", "92a5bd269eb82c7c-7665408064f662f5"},
		{"EngineAssets/Shaders/VisualEffects/ParticleArguments.hlsl.meta", "a36913133ad11452-77bf07bede64e8c2"},
		{"EngineAssets/Shaders/VisualEffects/ParticleSortLocal.hlsl.meta", "66e4f1ad6ec2fc46-33d74ec651d42d9c"},
		{"EngineAssets/Shaders/VisualEffects/ParticleSortMerge.hlsl.meta", "6794b974e5c1d399-cbe48280c5c87815"},
		{"EngineAssets/Shaders/VisualEffects/ParticleStrips.hlsl.meta", "4ba52ceb82478c10-4471540b3740698c"},
	};
	for (const Expected& kernel : kernels) {
		std::filesystem::path meta = EffectsRepoDir(kernel.path);
		ASSERT_FALSE(meta.empty()) << kernel.path;
		str text = FileSystem::ReadTextFile(Path(meta.generic_string()), MemTag::Asset);
		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		JSONValue root = JSONParse(text, arena);
		EXPECT_EQ(Guid::FromString(root["assetId"].AsString()), Guid::FromString(kernel.guid)) << kernel.path;
	}
#ifndef SHALLTEAR_SHARED
	EXPECT_EQ(VisualEffectsImpl::PARTICLE_RESET_KERNEL, Guid::FromString(kernels[0].guid));
	EXPECT_EQ(VisualEffectsImpl::PARTICLE_ARGUMENTS_KERNEL, Guid::FromString(kernels[1].guid));
	EXPECT_EQ(VisualEffectsImpl::PARTICLE_SORT_LOCAL_KERNEL, Guid::FromString(kernels[2].guid));
	EXPECT_EQ(VisualEffectsImpl::PARTICLE_SORT_MERGE_KERNEL, Guid::FromString(kernels[3].guid));
	EXPECT_EQ(VisualEffectsImpl::PARTICLE_STRIPS_KERNEL, Guid::FromString(kernels[4].guid));
#endif
}

#ifndef SHALLTEAR_SHARED

namespace {

	using VisualEffectsImpl::ParticleSystem;
	using VisualEffectsImpl::ParticleSystemDesc;

	struct Counts {
		u32 alive[2] = {};
		u32 dead = 0;
		u32 dropped = 0;
	};

	class ParticleSimulationFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		char _token = 0;
		ShaderAsset _initialize;
		ShaderAsset _update;

		void SetUp() override {
			std::filesystem::path engineAssets = EffectsRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			SLT_CALL_LINKED_PACKAGE(VisualEffects, &_token);
			std::filesystem::path fixtures = EffectsRepoDir("Source/Tests/Fixtures/VisualEffects");
			if (fixtures.empty() || !_initialize.Import(Path((fixtures / "AgeInitialize.hlsl").generic_string())) ||
				!_update.Import(Path((fixtures / "AgeUpdate.hlsl").generic_string()))) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
		}

		void TearDown() override {
			VisualEffectsImpl::ShutdownSimulation();
			FramePasses::UnregisterOwnedBy(&_token);
			FrameStages::UnregisterOwnedBy(&_token);
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}

		bool Create(ParticleSystem& system, u32 capacity, f32 lifetime, VisualEffectsImpl::ParticleBatchKey key = {}, u32 slots = 1) {
			ParticleSystemDesc desc;
			desc.capacity = capacity;
			desc.attributeCount = 2;
			desc.initialize = Handle<ShaderAsset>(&_initialize);
			desc.update = Handle<ShaderAsset>(&_update);
			desc.batch = key;
			desc.batchSlots = slots;
			if (!system.Create(desc)) {
				return false;
			}
			f32 frame[25] = {};
			frame[24] = lifetime;
			system.SetFrameData({reinterpret_cast<const u8*>(frame), sizeof(frame)});
			return true;
		}

		static Counts ReadCounts(const ParticleSystem& system) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			Counts counts;
			device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(&counts), sizeof(counts)}, system.GetListsOffset());
			return counts;
		}

		static u32 Alive(const ParticleSystem& system) {
			return ReadCounts(system).alive[system.GetParity()];
		}

		static u32 UpdateGroups(const ParticleSystem& system) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			u32 arguments[3] = {};
			device.ReadbackBuffer(system.GetArgumentsBuffer(), {reinterpret_cast<u8*>(arguments), sizeof(arguments)}, system.GetArgumentsOffset());
			return arguments[0];
		}

		static bool AliveIndicesAreDistinct(const ParticleSystem& system) {
			const u32 alive = Alive(system);
			if (alive == 0) {
				return true;
			}
			Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
			indices.assign(alive, 0);
			const u64 offset = system.GetListsOffset() + (4ull + static_cast<u64>(system.GetParity()) * system.GetCapacity()) * 4;
			RenderSystem::GetDevice()->ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(indices.data()), indices.size() * 4}, offset);
			std::sort(indices.begin(), indices.end());
			return std::adjacent_find(indices.begin(), indices.end()) == indices.end() && indices.back() < system.GetCapacity();
		}

		static void Frame(ParticleSystem& system, f32 deltaTime, u32 spawn) {
			system.Advance(deltaTime, spawn);
			RenderSystem::RenderAll();
		}
	};

} // anonymous namespace

TEST_F(ParticleSimulationFixture, SystemLifecycleUnderCapacity) {
	ParticleSystem system;
	ASSERT_TRUE(Create(system, 64, 2.5f));
	ParticleSystem other;
	ASSERT_TRUE(Create(other, 32, 100.0f));

	const u32 expected[] = {10, 20, 30, 30, 30, 30, 30, 30, 30, 30};
	for (u32 frame = 0; frame < 10; ++frame) {
		other.Advance(1.0f, 3);
		Frame(system, 1.0f, 10);
		const Counts counts = ReadCounts(system);
		const u32 alive = counts.alive[system.GetParity()];
		EXPECT_EQ(alive, expected[frame]) << "frame " << frame;
		EXPECT_EQ(alive + counts.dead, 64u) << "frame " << frame;
		EXPECT_EQ(counts.dropped, 0u) << "frame " << frame;
		EXPECT_EQ(UpdateGroups(system), (alive + 63) / 64) << "frame " << frame;
		EXPECT_TRUE(AliveIndicesAreDistinct(system)) << "frame " << frame;
	}
	EXPECT_EQ(system.GetSimulatedFrames(), 10u);
	EXPECT_EQ(Alive(other), 30u);

	for (u32 left : {20u, 10u, 0u}) {
		Frame(system, 1.0f, 0);
		EXPECT_EQ(Alive(system), left);
	}
	EXPECT_EQ(ReadCounts(system).dead, 64u);
	EXPECT_EQ(UpdateGroups(system), 0u);
}

TEST_F(ParticleSimulationFixture, AdvanceIsFrameUpdate) {
	ParticleSystem system;
	ASSERT_TRUE(Create(system, 64, 2.5f));
	system.Advance(1.0f, 10);
	system.Advance(1.0f, 10);
	Frame(system, 1.0f, 10);
	EXPECT_EQ(system.GetSimulatedFrames(), 3u);
	EXPECT_EQ(Alive(system), 30u);
	EXPECT_EQ(UpdateGroups(system), 1u);
	EXPECT_TRUE(AliveIndicesAreDistinct(system));
	Frame(system, 1.0f, 0);
	EXPECT_EQ(Alive(system), 20u);

	ParticleSystem restarted;
	ASSERT_TRUE(Create(restarted, 64, 100.0f));
	restarted.Advance(1.0f, 20);
	restarted.BeginUpdates();
	Frame(restarted, 1.0f, 5);
	EXPECT_EQ(restarted.GetSimulatedFrames(), 1u);
	EXPECT_EQ(Alive(restarted), 5u);
}

TEST_F(ParticleSimulationFixture, SpawnPastCapacityIsDroppedAndCounted) {
	ParticleSystem system;
	ASSERT_TRUE(Create(system, 16, 100.0f));
	Frame(system, 1.0f, 20);
	Counts counts = ReadCounts(system);
	EXPECT_EQ(counts.alive[system.GetParity()], 16u);
	EXPECT_EQ(counts.dead, 0u);
	EXPECT_EQ(counts.dropped, 4u);
	Frame(system, 1.0f, 20);
	counts = ReadCounts(system);
	EXPECT_EQ(counts.alive[system.GetParity()], 16u);
	EXPECT_EQ(counts.dropped, 24u);
	EXPECT_TRUE(AliveIndicesAreDistinct(system));
}

TEST_F(ParticleSimulationFixture, AFrameWithNoAdvanceSimulatesNothing) {
	ParticleSystem system;
	ASSERT_TRUE(Create(system, 64, 2.5f));
	ParticleSystem busy;
	ASSERT_TRUE(Create(busy, 64, 100.0f));
	busy.Advance(1.0f, 1);
	Frame(system, 1.0f, 10);
	const u32 parity = system.GetParity();
	const u32 alive = Alive(system);
	for (int frame = 0; frame < 2; ++frame) {
		Frame(busy, 1.0f, 1);
	}
	EXPECT_EQ(busy.GetSimulatedFrames(), 3u);
	EXPECT_EQ(system.GetSimulatedFrames(), 1u);
	EXPECT_EQ(system.GetParity(), parity);
	EXPECT_EQ(Alive(system), alive);
}

TEST_F(ParticleSimulationFixture, MidFlightReleaseWaitsForFrame) {
	{
		ParticleSystem warm;
		ASSERT_TRUE(Create(warm, 64, 2.5f));
		Frame(warm, 1.0f, 10);
	}
	for (int i = 0; i < 3; ++i) {
		RenderSystem::RenderAll();
	}
	RenderSystem::GetDevice()->WaitIdle();
	const u32 before = RenderSystem::GetStats().liveBuffers;
	{
		ParticleSystem system;
		ASSERT_TRUE(Create(system, 64, 2.5f));
		EXPECT_EQ(RenderSystem::GetStats().liveBuffers, before + 3);
		Frame(system, 1.0f, 10);
	}
	for (int i = 0; i < 3; ++i) {
		RenderSystem::RenderAll();
	}
	RenderSystem::GetDevice()->WaitIdle();
	EXPECT_EQ(RenderSystem::GetStats().liveBuffers, before);
	EXPECT_EQ(VisualEffectsImpl::GetSimulation().GetSystemCount(), 0u);
}

namespace {

	using VisualEffectsImpl::ParticleBatchKey;

	constexpr ParticleBatchKey SPARKS{1, 1, 0};

	u32 FrameDispatches() { return VisualEffectsImpl::GetSimulation().GetFrameCounts().dispatches; }
	u32 FrameBatches() { return VisualEffectsImpl::GetSimulation().GetFrameCounts().batches; }

	Vector<f32> Ages(const ParticleSystem& system) {
		RHI::Device& device = *RenderSystem::GetDevice();
		device.WaitIdle();
		u32 header[4] = {};
		device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(header), sizeof(header)}, system.GetListsOffset());
		const u32 alive = header[system.GetParity()];
		Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
		indices.assign(alive, 0);
		Vector<f32> ages{GetMemoryResource(MemTag::Graphics)};
		ages.assign(system.GetCapacity(), 0.0f);
		if (alive > 0) {
			device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(indices.data()), indices.size() * 4},
								  system.GetListsOffset() + (4ull + static_cast<u64>(system.GetParity()) * system.GetCapacity()) * 4);
		}
		device.ReadbackBuffer(system.GetAttributesBuffer(), {reinterpret_cast<u8*>(ages.data()), ages.size() * 4}, system.GetAttributesOffset());
		Vector<f32> out{GetMemoryResource(MemTag::Graphics)};
		for (u32 index : indices) {
			out.push_back(index < ages.size() ? ages[index] : -1.0f);
		}
		return out;
	}

	u32 CountOf(const Vector<f32>& values, f32 value) { return static_cast<u32>(std::count(values.begin(), values.end(), value)); }

} // anonymous namespace

TEST_F(ParticleSimulationFixture, SameKeySharesBatch) {
	u32 alone[2] = {};
	{
		ParticleSystem system;
		ASSERT_TRUE(Create(system, 64, 2.5f));
		Frame(system, 1.0f, 10);
		alone[0] = FrameDispatches();
		Frame(system, 1.0f, 10);
		alone[1] = FrameDispatches();
	}
	ASSERT_EQ(alone[0], 4u);
	ASSERT_EQ(alone[1], 3u);

	ParticleSystem systems[9];
	for (u32 i = 0; i < 8; ++i) {
		ASSERT_TRUE(Create(systems[i], 64, 2.5f, SPARKS, 8));
		ASSERT_EQ(systems[i].GetBatch(), systems[0].GetBatch()) << "system " << i;
		EXPECT_EQ(systems[i].GetSlot(), i);
	}
	for (u32 frame = 0; frame < 6; ++frame) {
		for (u32 i = 0; i < 8; ++i) {
			systems[i].Advance(1.0f, i + 1);
		}
		RenderSystem::RenderAll();
		EXPECT_EQ(FrameDispatches(), alone[frame == 0 ? 0 : 1]);
		EXPECT_EQ(FrameBatches(), 1u);
		for (u32 i = 0; i < 8; ++i) {
			const Counts counts = ReadCounts(systems[i]);
			const u32 alive = counts.alive[systems[i].GetParity()];
			EXPECT_EQ(alive, (i + 1) * std::min(frame + 1, 3u)) << "slot " << i << ", frame " << frame;
			EXPECT_EQ(alive + counts.dead, 64u) << "slot " << i << ", frame " << frame;
			EXPECT_EQ(counts.dropped, 0u);
			EXPECT_EQ(UpdateGroups(systems[i]), (alive + 63) / 64);
			EXPECT_TRUE(AliveIndicesAreDistinct(systems[i])) << "slot " << i << ", frame " << frame;
			EXPECT_EQ(systems[i].GetSimulatedFrames(), frame + 1);
		}
	}
	const VisualEffectsImpl::ParticleBatch& batch = *systems[0].GetBatch();
	RenderSystem::GetDevice()->WaitIdle();
	u32 dispatch[3] = {};
	RenderSystem::GetDevice()->ReadbackBuffer(batch.arguments, {reinterpret_cast<u8*>(dispatch), sizeof(dispatch)}, batch.DispatchOffset());
	EXPECT_EQ(dispatch[0], 1u);
	EXPECT_EQ(dispatch[1], 8u);
	EXPECT_EQ(dispatch[2], 1u);

	ASSERT_TRUE(Create(systems[8], 64, 2.5f, SPARKS, 8));
	EXPECT_NE(systems[8].GetBatch(), systems[0].GetBatch());
	EXPECT_EQ(systems[8].GetSlot(), 0u);
	for (u32 i = 0; i < 9; ++i) {
		systems[i].Advance(1.0f, 1);
	}
	RenderSystem::RenderAll();
	EXPECT_EQ(FrameBatches(), 2u);
	EXPECT_EQ(FrameDispatches(), alone[0] + alone[1]);
	EXPECT_EQ(Alive(systems[8]), 1u);
	EXPECT_EQ(Alive(systems[7]), 8u * 2u + 1u);
}

TEST_F(ParticleSimulationFixture, EachSlotOfBatchRunsUpdates) {
	ParticleSystem twice, once, paused;
	ASSERT_TRUE(Create(twice, 256, 100.0f, SPARKS, 4));
	ASSERT_TRUE(Create(once, 256, 100.0f, SPARKS, 4));
	ASSERT_TRUE(Create(paused, 256, 100.0f, SPARKS, 4));
	ASSERT_EQ(paused.GetBatch(), twice.GetBatch());
	twice.Advance(1.0f, 150);
	once.Advance(1.0f, 4);
	paused.Advance(1.0f, 4);
	RenderSystem::RenderAll();
	const u32 parity = paused.GetParity();
	const u32 frames = paused.GetSimulatedFrames();

	twice.Advance(1.0f, 2);
	twice.Advance(1.0f, 2);
	once.Advance(1.0f, 2);
	RenderSystem::RenderAll();
	EXPECT_EQ(FrameDispatches(), 6u);
	EXPECT_EQ(twice.GetSimulatedFrames(), 3u);
	EXPECT_EQ(once.GetSimulatedFrames(), 2u);
	EXPECT_EQ(paused.GetSimulatedFrames(), frames);
	EXPECT_EQ(paused.GetParity(), parity);

	const Vector<f32> twiceAges = Ages(twice);
	ASSERT_EQ(twiceAges.size(), 154u);
	EXPECT_EQ(CountOf(twiceAges, 2.0f), 150u);
	EXPECT_EQ(CountOf(twiceAges, 1.0f), 2u);
	EXPECT_EQ(CountOf(twiceAges, 0.0f), 2u);
	const Vector<f32> onceAges = Ages(once);
	ASSERT_EQ(onceAges.size(), 6u);
	EXPECT_EQ(CountOf(onceAges, 1.0f), 4u);
	EXPECT_EQ(CountOf(onceAges, 0.0f), 2u);
	const Vector<f32> pausedAges = Ages(paused);
	ASSERT_EQ(pausedAges.size(), 4u);
	EXPECT_EQ(CountOf(pausedAges, 0.0f), 4u);

	twice.Advance(1.0f, 1);
	once.Advance(1.0f, 1);
	paused.Advance(1.0f, 1);
	RenderSystem::RenderAll();
	const Counts counts = ReadCounts(paused);
	EXPECT_EQ(counts.alive[paused.GetParity()], 5u);
	EXPECT_EQ(counts.alive[paused.GetParity()] + counts.dead, 256u);
	EXPECT_TRUE(AliveIndicesAreDistinct(paused));
}

TEST_F(ParticleSimulationFixture, SlotLeftIsResetForNextSystem) {
	ParticleSystem first, last;
	ASSERT_TRUE(Create(first, 32, 100.0f, SPARKS, 4));
	u32 slot = 0;
	{
		ParticleSystem middle;
		ASSERT_TRUE(Create(middle, 32, 100.0f, SPARKS, 4));
		ASSERT_TRUE(Create(last, 32, 100.0f, SPARKS, 4));
		first.Advance(1.0f, 5);
		middle.Advance(1.0f, 40);
		last.Advance(1.0f, 7);
		RenderSystem::RenderAll();
		ASSERT_EQ(ReadCounts(middle).dropped, 8u);
		slot = middle.GetSlot();
	}
	ParticleSystem next;
	ASSERT_TRUE(Create(next, 32, 100.0f, SPARKS, 4));
	EXPECT_EQ(next.GetBatch(), first.GetBatch());
	EXPECT_EQ(next.GetSlot(), slot);
	first.Advance(1.0f, 1);
	next.Advance(1.0f, 3);
	last.Advance(1.0f, 1);
	RenderSystem::RenderAll();
	const Counts counts = ReadCounts(next);
	EXPECT_EQ(counts.alive[next.GetParity()], 3u);
	EXPECT_EQ(counts.dead, 29u);
	EXPECT_EQ(counts.dropped, 0u);
	EXPECT_TRUE(AliveIndicesAreDistinct(next));
	EXPECT_EQ(Alive(first), 6u);
	EXPECT_EQ(Alive(last), 8u);
}

TEST_F(ParticleSimulationFixture, EmptyBatchFreedAfterLifetime) {
	VisualEffectsImpl::ParticleSimulation& simulation = VisualEffectsImpl::GetSimulation();
	simulation.SetBatchEmptyLifetime(3);
	{
		ParticleSystem warm;
		ASSERT_TRUE(Create(warm, 64, 100.0f));
		Frame(warm, 1.0f, 10);
	}
	for (int i = 0; i < 3; ++i) {
		RenderSystem::RenderAll();
	}
	RenderSystem::GetDevice()->WaitIdle();
	ASSERT_EQ(simulation.GetBatchCount(), 0u);
	const u32 before = RenderSystem::GetStats().liveBuffers;
	{
		ParticleSystem system;
		ASSERT_TRUE(Create(system, 64, 100.0f, SPARKS, 4));
		EXPECT_EQ(RenderSystem::GetStats().liveBuffers, before + 3);
		Frame(system, 1.0f, 10);
	}
	RenderSystem::RenderAll();
	EXPECT_EQ(simulation.GetBatchCount(), 1u);
	{
		ParticleSystem again;
		ASSERT_TRUE(Create(again, 64, 100.0f, SPARKS, 4));
		EXPECT_EQ(RenderSystem::GetStats().liveBuffers, before + 3);
		Frame(again, 1.0f, 2);
		EXPECT_EQ(Alive(again), 2u);
	}
	RenderSystem::RenderAll();
	RenderSystem::RenderAll();
	EXPECT_EQ(simulation.GetBatchCount(), 1u);
	RenderSystem::RenderAll();
	EXPECT_EQ(simulation.GetBatchCount(), 0u);
	for (int i = 0; i < 3; ++i) {
		RenderSystem::RenderAll();
	}
	RenderSystem::GetDevice()->WaitIdle();
	EXPECT_EQ(RenderSystem::GetStats().liveBuffers, before);
}

TEST_F(ParticleSimulationFixture, SharingNeedsLayoutNoGpuEvent) {
	ParticleSystem a, b, plain, wider, eventful;
	ASSERT_TRUE(Create(a, 64, 100.0f, SPARKS, 4));
	ASSERT_TRUE(Create(b, 64, 100.0f, SPARKS, 4));
	ASSERT_TRUE(Create(plain, 64, 100.0f));
	ASSERT_TRUE(Create(wider, 128, 100.0f, SPARKS, 4));
	EXPECT_EQ(a.GetBatch(), b.GetBatch());
	EXPECT_NE(plain.GetBatch(), a.GetBatch());
	EXPECT_NE(wider.GetBatch(), a.GetBatch());
	ParticleSystemDesc desc;
	desc.capacity = 64;
	desc.attributeCount = 2;
	desc.initialize = Handle<ShaderAsset>(&_initialize);
	desc.update = Handle<ShaderAsset>(&_update);
	desc.batch = SPARKS;
	desc.batchSlots = 4;
	desc.updateEvents.push_back({&plain, 4, 5});
	ASSERT_TRUE(eventful.Create(desc));
	EXPECT_NE(eventful.GetBatch(), a.GetBatch());
	EXPECT_EQ(eventful.GetSlot(), 0u);
}

#endif
