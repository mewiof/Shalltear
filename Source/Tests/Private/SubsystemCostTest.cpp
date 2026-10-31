#include "TestsPCH.h"

#include "ProductLookup.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/Window.h"

#include <filesystem>

using namespace Shalltear;

namespace {

	struct LiveTotals {
		size_t allocations = 0;
		size_t bytes = 0;
	};

	LiveTotals Live() {
		return {MemoryManager::GetTotalAllocations(), MemoryManager::GetTotalBytes()};
	}

	i64 AllocationsSince(const LiveTotals& before) {
		return static_cast<i64>(Live().allocations) - static_cast<i64>(before.allocations);
	}

	i64 BytesSince(const LiveTotals& before) {
		return static_cast<i64>(Live().bytes) - static_cast<i64>(before.bytes);
	}

	void Report(const char* id, const str& reading) {
		std::cout << "[" << id << "] " << reading << "\n";
	}

	const Profiler::ZoneResult* FindZone(const Profiler::FrameResults& frame, std::string_view name) {
		for (u32 i = 0; i < frame.count; ++i) {
			if (frame.zones[i].name == name) {
				return &frame.zones[i];
			}
		}
		return nullptr;
	}

	class ZoneScope {
	public:
		ZoneScope() {
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
			Profiler::SetEnabled(true);
		}
		~ZoneScope() {
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
		}

		ZoneScope(const ZoneScope&) = delete;
		ZoneScope& operator=(const ZoneScope&) = delete;
	};

} // anonymous namespace

namespace {

	i32 _scTickCalls = 0;

	class ScTicker : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(ScTicker, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnUpdate() override { ++_scTickCalls; }
	};

	class ScPlain : public Component {
	public:
		i32 slot = 0;
		f32 weight = 0.0f;

		SLT_REFLECT_COMPONENT(ScPlain, Component)
		SLT_REFLECT_FIELD(slot)
		SLT_REFLECT_FIELD(weight)
		SLT_END_REFLECT()
	};

} // anonymous namespace

SLT_REGISTER_TYPE(ScTicker)
SLT_REGISTER_TYPE(ScPlain)

namespace {

	void RunSceneFrame() {
		SceneSystem::BeginFrame();
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
	}

	i32 TickCallsInOneFrame(u32 tickers, u32 idleObjects) {
		SceneSystem::Initialize();
		Handle<Scene> scene = SceneSystem::CreateScene("Cost");
		for (u32 i = 0; i < tickers; ++i) {
			SceneSystem::CreateObject("T", scene).Get()->AddComponent<ScTicker>();
		}
		for (u32 i = 0; i < idleObjects; ++i) {
			SceneSystem::CreateObject("I", scene).Get()->AddComponent<ScPlain>();
		}
		RunSceneFrame();
		_scTickCalls = 0;
		RunSceneFrame();
		const i32 calls = _scTickCalls;
		SceneSystem::Terminate();
		return calls;
	}

} // anonymous namespace

TEST(SceneCostTest, TickDispatchCountsTickersNotObjects) {
	const i32 lean = TickCallsInOneFrame(32, 0);
	const i32 fat = TickCallsInOneFrame(32, 512);
	Report("S1a", "tick dispatches: 32 tickers + 0 idle objects = " + std::to_string(lean) + ", 32 + 512 = " + std::to_string(fat));
	EXPECT_EQ(lean, 32);
	EXPECT_EQ(fat, lean);
}

TEST(SceneCostTest, AnIdleSceneFrameRetainsNothing) {
	SceneSystem::Initialize();
	Handle<Scene> scene = SceneSystem::CreateScene("Idle");
	for (u32 i = 0; i < 256; ++i) {
		SceneSystem::CreateObject("T", scene).Get()->AddComponent<ScTicker>();
	}
	RunSceneFrame();

	const LiveTotals before = Live();
	for (u32 frame = 0; frame < 240; ++frame) {
		RunSceneFrame();
	}
	const i64 allocations = AllocationsSince(before);
	const i64 bytes = BytesSince(before);
	Report("S1b", "240 idle frames over 256 tickers retained " + std::to_string(allocations) + " allocations, " + std::to_string(bytes) + " bytes");
	SceneSystem::Terminate();
	EXPECT_EQ(allocations, 0);
	EXPECT_EQ(bytes, 0);
}

TEST(SceneCostTest, CreateDestroyReturnsToStart) {
	SceneSystem::Initialize();
	Handle<Scene> scene = SceneSystem::CreateScene("Churn");
	Handle<SceneObject> warm = SceneSystem::CreateObject("W", scene);
	warm.Get()->AddComponent<ScTicker>();
	SceneSystem::Destroy(warm);
	RunSceneFrame();

	const LiveTotals before = Live();
	for (u32 cycle = 0; cycle < 200; ++cycle) {
		Handle<SceneObject> object = SceneSystem::CreateObject("C", scene);
		object.Get()->AddComponent<ScTicker>();
		SceneSystem::Destroy(object);
		RunSceneFrame();
	}
	const i64 allocations = AllocationsSince(before);
	const i64 bytes = BytesSince(before);
	Report("S1c", "200 create/destroy cycles retained " + std::to_string(allocations) + " allocations, " + std::to_string(bytes) + " bytes");
	SceneSystem::Terminate();
	EXPECT_EQ(allocations, 0);
	EXPECT_EQ(bytes, 0);
}

TEST(ReflectionCostTest, FieldReadWriteRoundTripRetainsNothing) {
	TypeRegistry::RegisterPendingTypes();
	const TypeInfo* type = TypeRegistry::FindType(StringHash("ScPlain"));
	ASSERT_NE(type, nullptr);
	ASSERT_EQ(type->fieldCount, 2u);
	ScPlain instance;
	instance.slot = 7;

	Variant warm;
	type->fields[0].getFn(&instance, &warm);

	const LiveTotals before = Live();
	for (u32 i = 0; i < 4096; ++i) {
		Variant value;
		type->fields[0].getFn(&instance, &value);
		type->fields[0].setFn(&instance, &value);
	}
	const i64 allocations = AllocationsSince(before);
	Report("S2a", "4096 reflected get/set pairs retained " + std::to_string(allocations) + " allocations");
	EXPECT_EQ(instance.slot, 7);
	EXPECT_EQ(allocations, 0);
}

TEST(ReflectionCostTest, ATypeLookupRetainsNothing) {
	TypeRegistry::RegisterPendingTypes();
	const u32 registered = TypeRegistry::GetTypeCount();
	ASSERT_GT(registered, 0u);
	(void)TypeRegistry::FindType(StringHash("ScPlain"));

	const LiveTotals before = Live();
	for (u32 i = 0; i < 4096; ++i) {
		ASSERT_NE(TypeRegistry::FindType(StringHash("ScPlain")), nullptr);
	}
	const i64 allocations = AllocationsSince(before);
	Report("S2b", "4096 FindType calls against " + std::to_string(registered) + " registered types retained " + std::to_string(allocations) + " allocations");
	EXPECT_EQ(allocations, 0);
	EXPECT_EQ(TypeRegistry::GetTypeCount(), registered);
}

namespace {

	str MakeEscapedDocument(u32 elements) {
		str text = "{\"items\":[";
		for (u32 i = 0; i < elements; ++i) {
			if (i > 0) {
				text += ",";
			}
			text += "{\"id\":";
			text += std::to_string(i);
			text += ",\"name\":\"a\\u00e9b\\tc\\\"d\"}";
		}
		text += "]}";
		return text;
	}

	size_t ArenaBytesForDocument(u32 elements) {
		const str text = MakeEscapedDocument(elements);
		LinearAllocator arena(8 * 1024 * 1024, MemTag::Core);
		JSONValue root = JSONParse(text, arena);
		EXPECT_EQ(root.type, JSONValue::Type::Object);
		EXPECT_EQ(root["items"].Size(), elements);
		EXPECT_EQ(root["items"][size_t{0}]["name"].AsString(), "a\xC3\xA9\x62\t"
															   "c\"d");
		return arena.GetUsedBytes();
	}

} // anonymous namespace

TEST(SerializationCostTest, ArenaBytesPerElementFlat) {
	const size_t small = ArenaBytesForDocument(256);
	const size_t large = ArenaBytesForDocument(1024);
	const f64 perSmall = static_cast<f64>(small) / 256.0;
	const f64 perLarge = static_cast<f64>(large) / 1024.0;
	Report("S3a", "arena bytes per escaped element: 256 elements = " + std::to_string(perSmall) + " (" + std::to_string(small) + " total), 1024 = " + std::to_string(perLarge) + " (" + std::to_string(large) + " total)");
	EXPECT_GT(small, 0u);
	EXPECT_LE(perLarge, perSmall * 1.05);
}

TEST(SerializationCostTest, RepeatedParsesLandOnSameArenaMark) {
	const str text = MakeEscapedDocument(256);
	LinearAllocator arena(8 * 1024 * 1024, MemTag::Core);
	size_t firstMark = 0;
	size_t lastMark = 0;
	for (u32 i = 0; i < 32; ++i) {
		{
			JSONValue root = JSONParse(text, arena);
			ASSERT_EQ(root.type, JSONValue::Type::Object);
			lastMark = arena.GetUsedBytes();
			if (i == 0) {
				firstMark = lastMark;
			}
		}
		arena.Reset();
		ASSERT_EQ(arena.GetUsedBytes(), 0u);
	}
	Report("S3b", "32 parses of a 256-element document: first mark " + std::to_string(firstMark) + " bytes, last " + std::to_string(lastMark));
	EXPECT_GT(firstMark, 0u);
	EXPECT_EQ(lastMark, firstMark);
}

namespace {

	class AssetCostFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_assetcost_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void WriteSource(const str& virtualPath, const str& content) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, content);
		}
	};

} // anonymous namespace

TEST_F(AssetCostFixture, ALookupRetainsNothing) {
	WriteSource("project://Assets/readme.txt", "hello shalltear");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/readme.txt"));
	ASSERT_NE(asset, nullptr);
	const Guid known = asset->assetId;
	(void)AssetDatabase::LookupGuid(Path("project://Assets/readme.txt"));

	const LiveTotals before = Live();
	for (u32 i = 0; i < 2048; ++i) {
		(void)AssetDatabase::LookupGuid(Path("project://Assets/readme.txt"));
		(void)AssetDatabase::LookupGuid(Path("project://Assets/absent.txt"));
		(void)AssetDatabase::LookupPath(known);
	}
	const i64 allocations = AllocationsSince(before);
	Report("S4a", "6144 lookups (hit, miss, reverse) retained " + std::to_string(allocations) + " allocations");
	AssetDatabase::UnloadAsset(asset);
	EXPECT_EQ(allocations, 0);
}

TEST_F(AssetCostFixture, LoadUnloadReturnsToStart) {
	WriteSource("project://Assets/readme.txt", "hello shalltear");
	Asset* warm = AssetDatabase::LoadAsset(Path("project://Assets/readme.txt"));
	ASSERT_NE(warm, nullptr);
	AssetDatabase::UnloadAsset(warm);

	const LiveTotals before = Live();
	for (u32 cycle = 0; cycle < 64; ++cycle) {
		Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/readme.txt"));
		ASSERT_NE(asset, nullptr);
		AssetDatabase::UnloadAsset(asset);
	}
	const i64 allocations = AllocationsSince(before);
	const i64 bytes = BytesSince(before);
	Report("S4b", "64 load/unload cycles retained " + std::to_string(allocations) + " allocations, " + std::to_string(bytes) + " bytes");
	EXPECT_EQ(allocations, 0);
	EXPECT_EQ(bytes, 0);
}

TEST(JobCostTest, SubmittingAndWaitingRetainsNothingPerJob) {
	JobSystem::Initialize();
	std::atomic<i32> ran{0};
	JobSystem::WaitFor(JobSystem::Submit([&ran]() { ++ran; }));

	const LiveTotals before = Live();
	for (u32 i = 0; i < 512; ++i) {
		JobSystem::WaitFor(JobSystem::Submit([&ran]() { ++ran; }));
	}
	const i64 allocations = AllocationsSince(before);
	const i64 bytes = BytesSince(before);
	Report("S5a", "512 submit/wait pairs retained " + std::to_string(allocations) + " allocations, " + std::to_string(bytes) + " bytes");
	JobSystem::Terminate();
	EXPECT_EQ(ran.load(), 513);
	EXPECT_EQ(allocations, 0);
	EXPECT_EQ(bytes, 0);
}

TEST(JobCostTest, FanOutRetainsNothing) {
	JobSystem::Initialize();
	std::atomic<i32> ran{0};
	{
		Vector<JobHandle> warm{GetMemoryResource(MemTag::Core)};
		for (u32 i = 0; i < 256; ++i) {
			warm.push_back(JobSystem::Submit([&ran]() { ++ran; }));
		}
		JobSystem::WaitForAll(std::span<const JobHandle>(warm.data(), warm.size()));
	}

	const LiveTotals before = Live();
	{
		Vector<JobHandle> handles{GetMemoryResource(MemTag::Core)};
		handles.reserve(256);
		for (u32 i = 0; i < 256; ++i) {
			handles.push_back(JobSystem::Submit([&ran]() { ++ran; }));
		}
		JobSystem::WaitForAll(std::span<const JobHandle>(handles.data(), handles.size()));
	}
	const i64 allocations = AllocationsSince(before);
	Report("S5b", "a 256-wide fan-out retained " + std::to_string(allocations) + " allocations");
	JobSystem::Terminate();
	EXPECT_EQ(ran.load(), 512);
	EXPECT_EQ(allocations, 0);
}

TEST(ModuleCostTest, LoadUnloadReturnsToStart) {
	ModuleSystem::Initialize();
	const Path binary("ShalltearSampleModule.dll");

	TypeRegistry::RegisterPendingTypes();
	const u32 typesAtEntry = TypeRegistry::GetTypeCount();

	ModuleHandle warm = ModuleSystem::Load(binary);
	if (!ModuleSystem::IsLoaded(warm)) {
		ModuleSystem::Terminate();
		GTEST_SKIP() << "ShalltearSampleModule.dll not found";
	}
	EXPECT_GT(TypeRegistry::GetTypeCount(), typesAtEntry);
	ASSERT_TRUE(ModuleSystem::Unload(warm));

	const LiveTotals before = Live();
	for (u32 cycle = 0; cycle < 8; ++cycle) {
		ModuleHandle handle = ModuleSystem::Load(binary);
		ASSERT_TRUE(ModuleSystem::IsLoaded(handle));
		ASSERT_TRUE(ModuleSystem::Unload(handle));
	}
	const i64 allocations = AllocationsSince(before);
	const i64 bytes = BytesSince(before);
	const u32 typesAfter = TypeRegistry::GetTypeCount();
	Report("S6a", "8 load/unload cycles retained " + std::to_string(allocations) + " allocations, " + std::to_string(bytes) + " bytes; registry " + std::to_string(typesAtEntry) + " -> " + std::to_string(typesAfter) + " types");
	ModuleSystem::Terminate();
	EXPECT_EQ(typesAfter, typesAtEntry);
	EXPECT_EQ(allocations, 0);
	EXPECT_EQ(bytes, 0);
}

TEST(SubsystemZoneTest, ZonesEnteredOncePerOperation) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	ZoneScope zones;
	SceneSystem::Initialize();
	SceneSystem::CreateScene("Zones");

	for (u32 frame = 0; frame < 3; ++frame) {
		RunSceneFrame();
		Profiler::NextFrame();
		const Profiler::FrameResults results = Profiler::GetFrameResults();

		const Profiler::ZoneResult* update = FindZone(results, "Scene.Update");
		ASSERT_NE(update, nullptr) << frame;
		EXPECT_EQ(update->calls, 1u) << "UpdateAll ran " << update->calls;

		const Profiler::ZoneResult* endFrame = FindZone(results, "Scene.EndFrame");
		ASSERT_NE(endFrame, nullptr) << frame;
		EXPECT_EQ(endFrame->calls, 1u) << "EndFrame ran " << endFrame->calls;
	}
	SceneSystem::Terminate();

	ModuleSystem::Initialize();
	ModuleHandle handle = ModuleSystem::Load(Path("ShalltearSampleModule.dll"));
	const bool moduleLoaded = ModuleSystem::IsLoaded(handle);
	if (moduleLoaded) {
		ModuleSystem::Unload(handle);
	}
	ModuleSystem::Terminate();
	Profiler::NextFrame();
	if (moduleLoaded) {
		const Profiler::ZoneResult* load = FindZone(Profiler::GetFrameResults(), "Module.Load");
		ASSERT_NE(load, nullptr);
		EXPECT_EQ(load->calls, 1u);
	}
#endif
}

TEST_F(AssetCostFixture, AssetZoneIsEnteredOncePerLoad) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	WriteSource("project://Assets/readme.txt", "hello shalltear");
	ZoneScope zones;

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/readme.txt"));
	ASSERT_NE(asset, nullptr);
	Profiler::NextFrame();
	const Profiler::ZoneResult* load = FindZone(Profiler::GetFrameResults(), "Asset.Load");
	ASSERT_NE(load, nullptr);
	EXPECT_EQ(load->calls, 1u);

	Asset* byGuid = AssetDatabase::LoadAssetByGuid(asset->assetId);
	ASSERT_EQ(byGuid, asset);
	Profiler::NextFrame();
	const Profiler::FrameResults second = Profiler::GetFrameResults();
	const Profiler::ZoneResult* byGuidZone = FindZone(second, "Asset.LoadByGuid");
	ASSERT_NE(byGuidZone, nullptr);
	EXPECT_EQ(byGuidZone->calls, 1u);
	const Profiler::ZoneResult* pathZone = FindZone(second, "Asset.Load");
	ASSERT_NE(pathZone, nullptr);
	EXPECT_EQ(pathZone->calls, 0u);

	AssetDatabase::UnloadAsset(byGuid);
	AssetDatabase::UnloadAsset(asset);
#endif
}

TEST(SubsystemZoneTest, UIZonesEnteredOncePerCall) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	desc.contentScale = 1.0f;
	UI::Context ctx{desc, nullptr};
	UI::DockManager dock{ctx, Handle<FontAsset>{}};
	dock.RegisterPanel("zone.panel"_hash, "P", [](UI::Context&, UI::ElementHandle) {});
	dock.DockPanel("zone.panel"_hash, dock.GetRootNode(), UI::DockZone::Center);

	ZoneScope zones;

	dock.Build(ctx.GetRoot());
	ctx.Update();
	Profiler::NextFrame();

	const Profiler::FrameResults frame = Profiler::GetFrameResults();
	const Profiler::ZoneResult* build = FindZone(frame, "UI.DockBuild");
	ASSERT_NE(build, nullptr);
	EXPECT_EQ(build->calls, 1u);
	const Profiler::ZoneResult* route = FindZone(frame, "UI.Route");
	ASSERT_NE(route, nullptr);
	EXPECT_EQ(route->calls, 1u);
	const Profiler::ZoneResult* settle = FindZone(frame, "UI.Settle");
	ASSERT_NE(settle, nullptr);
	EXPECT_EQ(settle->calls, 1u);

	ctx.LoadStyleSheet(".x { color: #ffffff; }");
	Profiler::NextFrame();
	const Profiler::ZoneResult* recalc = FindZone(Profiler::GetFrameResults(), "UI.RecalcAll");
	ASSERT_NE(recalc, nullptr);
	EXPECT_EQ(recalc->calls, 1u);
#endif
}

TEST(SubsystemZoneTest, RouteInputsTwoHalvesAreZonedApart) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	desc.contentScale = 1.0f;
	UI::Context ctx{desc, nullptr};

	ZoneScope zones;

	ctx.Update();
	Profiler::NextFrame();
	const Profiler::FrameResults quiet = Profiler::GetFrameResults();
	const Profiler::ZoneResult* route = FindZone(quiet, "UI.Route");
	ASSERT_NE(route, nullptr);
	const Profiler::ZoneResult* drain = FindZone(quiet, "UI.Drain");
	ASSERT_NE(drain, nullptr);
	const Profiler::ZoneResult* input = FindZone(quiet, "UI.Input");
	ASSERT_NE(input, nullptr);
	EXPECT_EQ(drain->calls, 1u);
	EXPECT_EQ(input->calls, 1u);
	EXPECT_EQ(drain->depth, route->depth + 1);
	EXPECT_EQ(input->depth, route->depth + 1);

	UI::ElementHandle doomed = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), doomed);
	ctx.DestroyElementNextUpdate(doomed);
	ASSERT_TRUE(ctx.IsAlive(doomed));
	ctx.Update();
	EXPECT_FALSE(ctx.IsAlive(doomed));
	Profiler::NextFrame();
	const Profiler::ZoneResult* drained = FindZone(Profiler::GetFrameResults(), "UI.Drain");
	ASSERT_NE(drained, nullptr);
	EXPECT_EQ(drained->calls, 1u);
#endif
}

TEST(SubsystemZoneTest, SettleStepsZonedApart) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	desc.contentScale = 1.0f;
	UI::Context ctx{desc, nullptr};
	UI::ElementHandle child = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), child);
	UI::ElementHandle popup = ctx.CreateElement();
	UI::Style popupStyle;
	popupStyle.width = UI::Length::Pixel(200.0f);
	popupStyle.height = UI::Length::Pixel(100.0f);
	ctx.SetStyle(popup, popupStyle);
	ctx.OpenPopup(popup, {0.0f, 0.0f}, UI::PopupFlags::Center);

	ZoneScope zones;
	ctx.ResetLayoutStats();

	ctx.Update();
	Profiler::NextFrame();

	const Profiler::FrameResults dirty = Profiler::GetFrameResults();
	const Profiler::ZoneResult* settle = FindZone(dirty, "UI.Settle");
	ASSERT_NE(settle, nullptr);
	const Profiler::ZoneResult* content = FindZone(dirty, "UI.ContentDirty");
	ASSERT_NE(content, nullptr);
	const Profiler::ZoneResult* popups = FindZone(dirty, "UI.Popups");
	ASSERT_NE(popups, nullptr);
	const Profiler::ZoneResult* hover = FindZone(dirty, "UI.Hover");
	ASSERT_NE(hover, nullptr);
	const Profiler::ZoneResult* tail = FindZone(dirty, "UI.SettleTail");
	ASSERT_NE(tail, nullptr);
	const Profiler::ZoneResult* layout = FindZone(dirty, "UI.Layout");
	ASSERT_NE(layout, nullptr);

	EXPECT_EQ(content->calls, 1u);
	EXPECT_EQ(popups->calls, 1u);
	EXPECT_EQ(tail->calls, 1u);
	EXPECT_EQ(content->depth, settle->depth + 1);
	EXPECT_EQ(popups->depth, settle->depth + 1);
	EXPECT_EQ(hover->depth, settle->depth + 1);
	EXPECT_EQ(tail->depth, settle->depth + 1);
	EXPECT_EQ(layout->depth, settle->depth + 1);

	ASSERT_EQ(ctx.GetLayoutStats().passes, 2u);
	EXPECT_EQ(static_cast<u64>(layout->calls), ctx.GetLayoutStats().passes);
	EXPECT_EQ(hover->calls, 1u);

	ctx.ResetLayoutStats();
	ctx.Update();
	Profiler::NextFrame();
	const Profiler::FrameResults clean = Profiler::GetFrameResults();
	const Profiler::ZoneResult* settleAgain = FindZone(clean, "UI.Settle");
	ASSERT_NE(settleAgain, nullptr);
	EXPECT_EQ(settleAgain->calls, 1u);
	const Profiler::ZoneResult* layoutAgain = FindZone(clean, "UI.Layout");
	ASSERT_NE(layoutAgain, nullptr);
	EXPECT_EQ(layoutAgain->calls, 0u);
	EXPECT_EQ(ctx.GetLayoutStats().passes, 0u);
	const Profiler::ZoneResult* hoverAgain = FindZone(clean, "UI.Hover");
	ASSERT_NE(hoverAgain, nullptr);
	EXPECT_EQ(hoverAgain->calls, 0u);
#endif
}

namespace {

	class RenderCostFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mounted = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_rendercost_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			}
			_mounted = true;

			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
				FileSystem::Unmount("project");
			}
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static std::filesystem::path RepoDir(const char* relative) {
			std::error_code ec;
			std::filesystem::path probe = std::filesystem::current_path(ec);
			for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
				if (std::filesystem::exists(probe / relative, ec)) {
					return probe / relative;
				}
				std::filesystem::path parent = probe.parent_path();
				if (parent == probe) {
					break;
				}
				probe = parent;
			}
			return {};
		}

		bool StageCube() {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / "cube.glb", _root / "cube.glb",
									   std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		Asset* LoadCubeMesh() {
			Asset* model = AssetDatabase::LoadAsset(Path("project://cube.glb"));
			if (!model) {
				return nullptr;
			}
			AssetDatabase::UnloadAsset(model);
			return ProductLookup::Load(Path("project://cube.glb"), "Meshes/Cube");
		}
	};

} // anonymous namespace

TEST_F(RenderCostFixture, MeshOutsideFrustumCostsNoDrawCall) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(StageCube());
	Asset* meshAsset = LoadCubeMesh();
	if (!meshAsset) {
		GTEST_SKIP() << "cube.glb import failed (dxcompiler.dll missing?)";
	}
	MeshAsset* mesh = static_cast<MeshAsset*>(meshAsset);
	ASSERT_FALSE(mesh->defaultMaterials.empty());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
	ASSERT_NE(materialAsset, nullptr);

	WindowDesc windowDesc;
	windowDesc.title = "RenderCostTest";
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("RenderCost");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 0.0f, -3.0f));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);

	Handle<SceneObject> cubeObject = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = cubeObject.Get()->AddComponent<MeshRenderer>();
	renderer.Get()->SetMesh(Handle<MeshAsset>(mesh));
	renderer.Get()->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));

	auto renderOneFrame = [&]() {
		WindowSystem::PumpEvents();
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
		return RenderSystem::GetStats().drawCalls;
	};

	renderOneFrame();
	const u32 inView = renderOneFrame();

	cubeObject.Get()->SetLocalPosition(Vec3(0.0f, 0.0f, -60.0f));
	const u32 outOfView = renderOneFrame();

	Report("S7a", "draw calls: cube in view = " + std::to_string(inView) + ", cube behind the camera = " + std::to_string(outOfView));
	EXPECT_GT(inView, outOfView);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(meshAsset);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(RenderCostFixture, RendererZoneOncePerFrame) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ZoneScope zones;
	for (u32 frame = 0; frame < 3; ++frame) {
		RenderSystem::RenderAll();
		Profiler::NextFrame();
		const Profiler::ZoneResult* zone = FindZone(Profiler::GetFrameResults(), "Render.Frame");
		ASSERT_NE(zone, nullptr) << frame;
		EXPECT_EQ(zone->calls, 1u) << "RenderAll ran " << zone->calls;
	}
#endif
}

TEST_F(RenderCostFixture, SixtyUnchangedFramesRetainNothing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	const GraphicsStats gpuAtEntry = RenderSystem::GetStats();

	ASSERT_TRUE(StageCube());
	Asset* meshAsset = LoadCubeMesh();
	if (!meshAsset) {
		GTEST_SKIP() << "cube.glb import failed (dxcompiler.dll missing?)";
	}
	MeshAsset* mesh = static_cast<MeshAsset*>(meshAsset);
	ASSERT_FALSE(mesh->defaultMaterials.empty());
	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
	ASSERT_NE(materialAsset, nullptr);

	WindowDesc windowDesc;
	windowDesc.title = "RenderCostTest";
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("RenderGrowth");
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject.Get()->SetLocalPosition(Vec3(0.0f, 0.0f, -3.0f));
	Handle<Camera> camera = cameraObject.Get()->AddComponent<Camera>();
	camera.Get()->SetTargetWindow(window);

	Handle<SceneObject> cubeObject = SceneSystem::CreateObject("Cube", scene);
	Handle<MeshRenderer> renderer = cubeObject.Get()->AddComponent<MeshRenderer>();
	renderer.Get()->SetMesh(Handle<MeshAsset>(mesh));
	renderer.Get()->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));

	auto renderOneFrame = [&]() {
		WindowSystem::PumpEvents();
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
	};

	for (u32 frame = 0; frame < 3; ++frame) {
		renderOneFrame();
	}

	const GraphicsStats gpuBefore = RenderSystem::GetStats();
	EXPECT_GT(gpuBefore.liveBuffers, gpuAtEntry.liveBuffers);
	EXPECT_GT(gpuBefore.liveTextures, gpuAtEntry.liveTextures);
	EXPECT_GT(gpuBefore.livePipelines, gpuAtEntry.livePipelines);

	const LiveTotals before = Live();
	for (u32 frame = 0; frame < 60; ++frame) {
		renderOneFrame();
	}
	const GraphicsStats gpuAfter = RenderSystem::GetStats();
	const i64 allocations = AllocationsSince(before);
	const i64 bytes = BytesSince(before);

	Report("S7b", "60 unchanged frames retained " + std::to_string(allocations) + " heap allocations and " + std::to_string(bytes) + " bytes; buffers " + std::to_string(gpuBefore.liveBuffers) + " -> " + std::to_string(gpuAfter.liveBuffers) + ", textures " + std::to_string(gpuBefore.liveTextures) + " -> " + std::to_string(gpuAfter.liveTextures) + ", pipelines " + std::to_string(gpuBefore.livePipelines) + " -> " + std::to_string(gpuAfter.livePipelines));
	EXPECT_EQ(allocations, 0);
	EXPECT_EQ(bytes, 0);
	EXPECT_EQ(gpuAfter.liveBuffers, gpuBefore.liveBuffers);
	EXPECT_EQ(gpuAfter.liveTextures, gpuBefore.liveTextures);
	EXPECT_EQ(gpuAfter.livePipelines, gpuBefore.livePipelines);
	EXPECT_EQ(gpuAfter.liveSamplers, gpuBefore.liveSamplers);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(meshAsset);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

namespace {

	class SamplingOverlay : public IWindowOverlay {
	public:
		Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Graphics)};
		Vector<Handle<Window>> recordedThisFrame{GetMemoryResource(MemTag::Graphics)};
		size_t graphicsBytesInsideFrame = 0;
		size_t graphicsAllocationsInsideFrame = 0;
		u32 calls = 0;

		void Sample() {
			size_t liveBytes = MemoryManager::GetBytesForTag(MemTag::Graphics);
			if (liveBytes > graphicsBytesInsideFrame) {
				graphicsBytesInsideFrame = liveBytes;
			}
			size_t liveAllocations = MemoryManager::GetAllocationCountForTag(MemTag::Graphics);
			if (liveAllocations > graphicsAllocationsInsideFrame) {
				graphicsAllocationsInsideFrame = liveAllocations;
			}
		}

		void CollectOverlayWindows(Vector<Handle<Window>>& out) override {
			recordedThisFrame.clear();
			for (Handle<Window> window : windows) {
				out.push_back(window);
			}
			Sample();
		}

		u32 RecordOverlay(Handle<Window> window, RHI::CommandList&, RHI::TextureHandle, Vec2I) override {
			Sample();
			recordedThisFrame.push_back(window);
			++calls;
			return 0;
		}
	};

	class InstalledOverlay {
	public:
		explicit InstalledOverlay(IWindowOverlay* overlay) { RenderSystem::SetWindowOverlay(overlay); }
		~InstalledOverlay() { RenderSystem::SetWindowOverlay(nullptr); }

		InstalledOverlay(const InstalledOverlay&) = delete;
		InstalledOverlay& operator=(const InstalledOverlay&) = delete;
	};

} // anonymous namespace

TEST_F(RenderCostFixture, FrameFreesWhatItAllocates) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	WindowDesc windowDesc;
	windowDesc.title = "RenderChurnTest";
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	SamplingOverlay overlay;
	overlay.windows.push_back(window);
	InstalledOverlay installed{&overlay};

	for (u32 frame = 0; frame < 3; ++frame) {
		WindowSystem::PumpEvents();
		RenderSystem::RenderAll();
	}
	ASSERT_GT(overlay.calls, 0u);

	overlay.graphicsBytesInsideFrame = 0;
	overlay.graphicsAllocationsInsideFrame = 0;
	WindowSystem::PumpEvents();
	RenderSystem::RenderAll();
	const size_t insideBytes = overlay.graphicsBytesInsideFrame;
	const size_t outsideBytes = MemoryManager::GetBytesForTag(MemTag::Graphics);
	const size_t insideAllocations = overlay.graphicsAllocationsInsideFrame;
	const size_t outsideAllocations = MemoryManager::GetAllocationCountForTag(MemTag::Graphics);

	const i64 byteChurn = static_cast<i64>(insideBytes) - static_cast<i64>(outsideBytes);
	const i64 allocationChurn = static_cast<i64>(insideAllocations) - static_cast<i64>(outsideAllocations);
	Report("S7c", "MemTag::Graphics inside the frame = " + std::to_string(insideAllocations) + " allocs / " + std::to_string(insideBytes) + " B, between frames = " + std::to_string(outsideAllocations) + " / " + std::to_string(outsideBytes) + ", churn = " + std::to_string(allocationChurn) + " allocs / " + std::to_string(byteChurn) + " B");
	EXPECT_EQ(allocationChurn, 0);
	EXPECT_EQ(byteChurn, 0) << "RenderAll allocated " << byteChurn;

	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(RenderCostFixture, DroppedWindowNotRendered) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	WindowDesc windowDesc;
	windowDesc.clientSize = {256, 256};
	windowDesc.flags = WindowFlags::Hidden;
	windowDesc.title = "RenderSlotA";
	Handle<Window> windowA = WindowSystem::Create(windowDesc);
	windowDesc.title = "RenderSlotB";
	Handle<Window> windowB = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(windowA.IsValid());
	ASSERT_TRUE(windowB.IsValid());

	SamplingOverlay overlay;
	overlay.windows.push_back(windowA);
	overlay.windows.push_back(windowB);
	InstalledOverlay installed{&overlay};

	WindowSystem::PumpEvents();
	RenderSystem::RenderAll();
	ASSERT_EQ(overlay.recordedThisFrame.size(), 2u);

	overlay.windows.pop_back();
	WindowSystem::PumpEvents();
	RenderSystem::RenderAll();

	Report("S7d", "windows recorded after dropping one: " + std::to_string(overlay.recordedThisFrame.size()));
	ASSERT_EQ(overlay.recordedThisFrame.size(), 1u);
	EXPECT_EQ(overlay.recordedThisFrame[0], windowA);

	overlay.windows.push_back(windowB);
	WindowSystem::PumpEvents();
	RenderSystem::RenderAll();
	ASSERT_EQ(overlay.recordedThisFrame.size(), 2u);
	EXPECT_EQ(overlay.recordedThisFrame[0], windowA);
	EXPECT_EQ(overlay.recordedThisFrame[1], windowB);

	WindowSystem::RequestDestroy(windowA);
	WindowSystem::RequestDestroy(windowB);
	WindowSystem::PumpEvents();
}
