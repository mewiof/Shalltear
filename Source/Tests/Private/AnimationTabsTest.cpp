#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "ImportPipeline.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>

#include <windows.h>

using namespace Shalltear;

namespace {

	constexpr const char* PACKAGE_TARGET = "Animation";

	const void* ImageOf(const void* address) {
		HMODULE image = nullptr;
		::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							 static_cast<const char*>(address), &image);
		return image;
	}

	std::filesystem::path RepoDir(const char* relative) {
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

	class AnimationTabsFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		ModuleHandle _module;
		const void* _moduleBase = nullptr;
		const void* _importPipelineBase = nullptr;
		bool _mountedEngine = false;
		Handle<Scene> _scene;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_tabs_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();

			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			_moduleBase = ModuleSystem::GetNativeHandle(_module);
			ASSERT_NE(_moduleBase, nullptr);
			AssetDatabase::Initialize();

			ASSERT_TRUE(EditorExtensions::Load(_moduleBase));

			_importPipelineBase = ImageOf(reinterpret_cast<const void*>(&RegisterImportPipeline));
			ASSERT_NE(_importPipelineBase, nullptr);
			ASSERT_TRUE(EditorExtensions::Load(_importPipelineBase));

			_scene = SceneSystem::CreateScene("rig-tab");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			if (_importPipelineBase) {
				EditorExtensions::Unload(_importPipelineBase);
			}
			EditorExtensions::Unload(_moduleBase);
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorAssetOps::Terminate();
			EditorSceneOps::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			ModuleSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StageFixture(const char* name) {
			return StageFixtureAs(name, name);
		}

		bool StageFixtureAs(const char* name, const char* asName) {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			const std::filesystem::path staged = _root / asName;
			std::error_code ec;
			std::filesystem::create_directories(staged.parent_path(), ec);
			std::filesystem::copy_file(fixtures / name, staged,
									   std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		bool WriteAnimationBlock(const char* modelVirtualPath, std::string_view blockJSON) {
			Path vpath(modelVirtualPath);
			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(vpath));
			str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
			if (text.empty()) {
				return false;
			}
			const size_t settings = text.find("\"settings\"");
			if (settings == str::npos) {
				return false;
			}
			const size_t open = text.find('{', settings);
			if (open == str::npos) {
				return false;
			}
			str spliced = text.substr(0, open + 1);
			spliced += blockJSON;
			str rest = text.substr(open + 1);
			const size_t firstReal = rest.find_first_not_of(" \t\r\n");
			if (firstReal != str::npos && rest[firstReal] != '}') {
				spliced += ",";
			}
			spliced += rest;
			return FileSystem::WriteTextFile(physMeta, spliced);
		}

		Handle<EditorPanel> _inspector;

		void BuildInspector(UI::Context& context, UI::ElementHandle& root) {
			_inspector = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
			ASSERT_TRUE(_inspector.IsValid());
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			_inspector.Get()->BuildContent(context, root);
			context.Update();
		}

		void Rebuild(UI::Context& context, UI::ElementHandle& root) {
			ASSERT_TRUE(_inspector.IsValid());
			context.ClearChildren(root);
			_inspector.Get()->BuildContent(context, root);
			context.Update();
		}
	};

} // namespace

TEST_F(AnimationTabsFixture, QuadrupedRigTabSaysNotAutoMapped) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid guid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	EditorSelection::SelectAsset(guid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 900};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	ASSERT_TRUE(UITest::FindByTextContaining(context, root, "Rig").IsValid());
	EXPECT_TRUE(UITest::FindByTextContaining(context, root, "no humanoid match").IsValid());
}

TEST_F(AnimationTabsFixture, NonModelPrefabHasNoRigTab) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	Guid guid = EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Unit.prefab"));
	ASSERT_TRUE(guid.IsValid());

	EditorSelection::SelectAsset(guid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 900};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	EXPECT_FALSE(UITest::FindByTextContaining(context, root, "Animation Type").IsValid());
}

namespace {

	Vector<str> ClipKeysOf(const Guid& model) {
		Vector<str> keys{GetMemoryResource(MemTag::Editor)};
		Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
		AssetDatabase::GetProducts(model, products);
		const StringHash clipType = StringHash("AnimationClip");
		for (const AssetProduct& product : products) {
			if (product.type == clipType) {
				keys.push_back(product.key);
			}
		}
		return keys;
	}

	Guid ClipGuidFor(const Guid& model, std::string_view key) {
		Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
		AssetDatabase::GetProducts(model, products);
		for (const AssetProduct& product : products) {
			if (product.key == key) {
				return product.guid;
			}
		}
		return {};
	}

} // namespace

TEST_F(AnimationTabsFixture, TakeSplitInto3ClipsSurvivesRestart) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	ASSERT_EQ(ClipKeysOf(modelGuid).size(), 3u);

	EditorSelection::SelectAsset(modelGuid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {700, 1800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	UI::ElementHandle tab = UITest::FindByText(context, root, "Animation");
	ASSERT_TRUE(tab.IsValid());

	UITest::ClickElement(context, tab);
	Rebuild(context, root);

	UI::ElementHandle add = UITest::FindByText(context, root, "+");
	ASSERT_TRUE(add.IsValid());
	UI::ElementHandle apply = UITest::FindByText(context, root, "Apply");
	ASSERT_TRUE(apply.IsValid());

	for (int i = 0; i < 3; ++i) {
		UITest::ClickElement(context, add);
	}
	UITest::ClickElement(context, apply);
	Rebuild(context, root);

	for (int i = 0; i < 3; ++i) {
		const str key = std::format("Clips/Clip{}", i);
		const Guid guid = ClipGuidFor(modelGuid, key);
		ASSERT_TRUE(guid.IsValid()) << key;
		Asset* clip = AssetDatabase::LoadAssetByGuid(guid);
		EXPECT_NE(clip, nullptr) << "'" << key;
		if (clip) {
			AssetDatabase::UnloadAsset(clip);
		}
	}

	const Guid survey = ClipGuidFor(modelGuid, "Clips/Survey");
	ASSERT_TRUE(survey.IsValid());
	AssetDatabase::CollectGarbage();
	Asset* dropped = AssetDatabase::LoadAssetByGuid(survey);
	EXPECT_EQ(dropped, nullptr);
	if (dropped) {
		AssetDatabase::UnloadAsset(dropped);
	}

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();

	Asset* reopened = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(reopened, nullptr);
	const Guid afterGuid = reopened->assetId;
	AssetDatabase::UnloadAsset(reopened);
	EXPECT_EQ(afterGuid, modelGuid);

	for (int i = 0; i < 3; ++i) {
		const str key = std::format("Clips/Clip{}", i);
		const Guid guid = ClipGuidFor(afterGuid, key);
		ASSERT_TRUE(guid.IsValid()) << "'" << key;
		Asset* clip = AssetDatabase::LoadAssetByGuid(guid);
		EXPECT_NE(clip, nullptr) << "'" << key;
		if (clip) {
			AssetDatabase::UnloadAsset(clip);
		}
	}
}

TEST_F(AnimationTabsFixture, MirrorOfferedOnlyForSidedRig) {
	if (!StageFixture("Fox.glb") || !StageFixture("HumanoidRig.gltf")) {
		GTEST_SKIP();
	}

	struct Row {
		const char* file;
		bool expected;
		const char* why;
	};
	const Row ROWS[] = {
		{"project://HumanoidRig.gltf", true, "auto mapped rig offered no Mirror"},
		{"project://Fox.glb", false, "quadruped offered Mirror"},
	};

	for (const Row& row : ROWS) {
		Asset* model = AssetDatabase::LoadAsset(Path(row.file));
		ASSERT_NE(model, nullptr) << row.file;
		const Guid modelGuid = model->assetId;
		AssetDatabase::UnloadAsset(model);

		EditorSelection::SelectAsset(modelGuid);
		UITest::MockFontSystem fonts;
		UI::ContextDesc desc;
		desc.sizePx = {700, 1800};
		UI::Context context(desc, &fonts);
		UI::ElementHandle root;
		BuildInspector(context, root);

		UI::ElementHandle tab = UITest::FindByText(context, root, "Animation");
		ASSERT_TRUE(tab.IsValid()) << row.file;
		UITest::ClickElement(context, tab);
		Rebuild(context, root);

		UI::ElementHandle add = UITest::FindByText(context, root, "+");
		ASSERT_TRUE(add.IsValid()) << row.file;
		UITest::ClickElement(context, add);
		Rebuild(context, root);

		ASSERT_TRUE(UITest::FindByText(context, root, "Start").IsValid());
		EXPECT_EQ(UITest::FindByText(context, root, "Mirror").IsValid(), row.expected) << row.why;
	}
}

TEST_F(AnimationTabsFixture, MirrorCheckboxWritesAndImportHonours) {
	if (!StageFixture("HumanoidRig.gltf")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://HumanoidRig.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	EditorSelection::SelectAsset(modelGuid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {700, 1800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	UI::ElementHandle tab = UITest::FindByText(context, root, "Animation");
	ASSERT_TRUE(tab.IsValid());
	UITest::ClickElement(context, tab);
	Rebuild(context, root);

	UI::ElementHandle add = UITest::FindByText(context, root, "+");
	ASSERT_TRUE(add.IsValid());
	UITest::ClickElement(context, add);
	UITest::ClickElement(context, add);
	Rebuild(context, root);

	UI::ElementHandle box = UITest::FindByClass(context, root, StringHash("slt-checkbox"), 1);
	ASSERT_TRUE(box.IsValid());
	UITest::ClickElement(context, box);

	UI::ElementHandle apply = UITest::FindByText(context, root, "Apply");
	ASSERT_TRUE(apply.IsValid());
	UITest::ClickElement(context, apply);
	Rebuild(context, root);

	const Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://HumanoidRig.gltf")));
	const str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
	EXPECT_NE(text.find("\"mirror\""), str::npos);

	Asset* reloaded = AssetDatabase::LoadAsset(Path("project://HumanoidRig.gltf"));
	ASSERT_NE(reloaded, nullptr);
	for (const char* key : {"Clips/Clip0", "Clips/Clip1"}) {
		const Guid guid = ClipGuidFor(modelGuid, key);
		ASSERT_TRUE(guid.IsValid()) << key;
		Asset* clip = AssetDatabase::LoadAssetByGuid(guid);
		EXPECT_NE(clip, nullptr);
		if (clip) {
			AssetDatabase::UnloadAsset(clip);
		}
	}
	AssetDatabase::UnloadAsset(reloaded);
}

TEST_F(AnimationTabsFixture, RangeKeepingTakeNameKeepsIdentity) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	const Guid before = ClipGuidFor(modelGuid, "Clips/Walk");
	ASSERT_TRUE(before.IsValid());

	ASSERT_TRUE(WriteAnimationBlock("project://Fox.glb",
									R"("animation":{"animationType":"Generic","clips":[)"
									R"({"name":"Walk","start":0.0,"end":0.05,"loop":"None"},)"
									R"({"name":"WalkTail","start":0.05,"end":0.1,"loop":"Loop"}]})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Fox.glb")));

	EXPECT_EQ(ClipGuidFor(modelGuid, "Clips/Walk"), before);
	EXPECT_TRUE(ClipGuidFor(modelGuid, "Clips/WalkTail").IsValid());
	EXPECT_TRUE(ClipGuidFor(modelGuid, "Clips/Survey").IsValid());
}

TEST_F(AnimationTabsFixture, SlicedClipIsLengthRangeStates) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	ASSERT_TRUE(WriteAnimationBlock("project://Fox.glb",
									R"("animation":{"animationType":"Generic","clips":[)"
									R"({"name":"Slice","start":0.017,"end":0.083,"loop":"None"}]})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Fox.glb")));

	const Guid sliceGuid = ClipGuidFor(modelGuid, "Clips/Slice");
	ASSERT_TRUE(sliceGuid.IsValid());
	Asset* slice = AssetDatabase::LoadAssetByGuid(sliceGuid);
	ASSERT_NE(slice, nullptr);

	const FieldInfo* lengthField = FindField(slice->GetType(), StringHash("length"));
	ASSERT_NE(lengthField, nullptr);
	ASSERT_NE(lengthField->addrFn, nullptr);
	f32 length = 0.0f;
	std::memcpy(&length, lengthField->addrFn(slice), sizeof(f32));
	EXPECT_NEAR(length, 0.066f, 1e-3f);
	AssetDatabase::UnloadAsset(slice);
}

TEST_F(AnimationTabsFixture, ApplyAloneReImports) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);
	ASSERT_EQ(ClipKeysOf(modelGuid).size(), 3u);

	ASSERT_TRUE(WriteAnimationBlock("project://Fox.glb", R"("animation":{"animationType":"None"})"));

	EditorSelection::SelectAsset(modelGuid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {700, 1800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	UI::ElementHandle tab = UITest::FindByText(context, root, "Animation");
	ASSERT_TRUE(tab.IsValid());
	UITest::ClickElement(context, tab);
	Rebuild(context, root);

	EXPECT_FALSE(AssetDatabase::ReimportAsset(Path("project://Fox.glb")));

	UI::ElementHandle apply = UITest::FindByText(context, root, "Apply");
	ASSERT_TRUE(apply.IsValid());
	UITest::ClickElement(context, apply);

	const Guid walk = ClipGuidFor(modelGuid, "Clips/Walk");
	ASSERT_TRUE(walk.IsValid());
	AssetDatabase::CollectGarbage();
	Asset* stillThere = AssetDatabase::LoadAssetByGuid(walk);
	EXPECT_EQ(stillThere, nullptr);
	if (stillThere) {
		AssetDatabase::UnloadAsset(stillThere);
	}
}

TEST_F(AnimationTabsFixture, TypeNoneDropsClipsKeepsRig) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);
	ASSERT_EQ(ClipKeysOf(modelGuid).size(), 3u);

	ASSERT_TRUE(WriteAnimationBlock("project://Fox.glb", R"("animation":{"animationType":"None"})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Fox.glb")));

	const Guid walk = ClipGuidFor(modelGuid, "Clips/Walk");
	ASSERT_TRUE(walk.IsValid());
	AssetDatabase::CollectGarbage();
	Asset* stillThere = AssetDatabase::LoadAssetByGuid(walk);
	EXPECT_EQ(stillThere, nullptr);
	if (stillThere) {
		AssetDatabase::UnloadAsset(stillThere);
	}

	Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
	AssetDatabase::GetProducts(modelGuid, products);
	const StringHash skeletonType = StringHash("SkeletonAsset");
	bool rigSurvived = false;
	for (const AssetProduct& product : products) {
		if (product.type == skeletonType) {
			rigSurvived = true;
		}
	}
	EXPECT_TRUE(rigSurvived);

	const Guid walkBefore = ClipGuidFor(modelGuid, "Clips/Walk");
	ASSERT_TRUE(walkBefore.IsValid());
	ASSERT_TRUE(WriteAnimationBlock("project://Fox.glb", R"("animation":{"animationType":"Generic"})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Fox.glb")));
	EXPECT_EQ(ClipGuidFor(modelGuid, "Clips/Walk"), walkBefore);
}

TEST_F(AnimationTabsFixture, NarrowedBoneListKeepsSkeletonWhole) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	size_t exposedBefore = 0;
	{
		Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid);
		ASSERT_TRUE(root.IsValid());
		Handle<Skeleton> skeletonHandle = root->GetComponent<Skeleton>();
		Skeleton* skeleton = skeletonHandle.Get();
		ASSERT_NE(skeleton, nullptr);
		exposedBefore = skeleton->GetExposedBoneCount();
		ASSERT_GT(exposedBefore, 2u);
		SceneSystem::Destroy(root);
		SceneSystem::EndFrame();
	}

	ASSERT_TRUE(WriteAnimationBlock("project://Fox.glb",
									R"("rig":{"exposedBones":["_rootJoint","b_Root_00"]})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Fox.glb")));

	{
		Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid);
		ASSERT_TRUE(root.IsValid());
		Handle<Skeleton> skeletonHandle = root->GetComponent<Skeleton>();
		Skeleton* skeleton = skeletonHandle.Get();
		ASSERT_NE(skeleton, nullptr);
		EXPECT_LT(skeleton->GetExposedBoneCount(), exposedBefore);
		for (u32 slot = 0; slot < skeleton->GetExposedBoneCount(); ++slot) {
			EXPECT_TRUE(skeleton->GetExposedObject(slot).IsValid());
		}
		SceneSystem::Destroy(root);
		SceneSystem::EndFrame();
	}

	const Guid skeletonGuid = ClipGuidFor(modelGuid, "Skeletons/Fox");
	ASSERT_TRUE(skeletonGuid.IsValid());
	Asset* asset = AssetDatabase::LoadAssetByGuid(skeletonGuid);
	ASSERT_NE(asset, nullptr);
	EXPECT_GT(static_cast<SkeletonAsset*>(asset)->boneNames.size(), 2u);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(AnimationTabsFixture, UntickedBoneNarrowsProjection) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	size_t exposedBefore = 0;
	{
		Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid);
		ASSERT_TRUE(root.IsValid());
		Skeleton* skeleton = root->GetComponent<Skeleton>().Get();
		ASSERT_NE(skeleton, nullptr);
		exposedBefore = skeleton->GetExposedBoneCount();
		ASSERT_GT(exposedBefore, 2u);
		SceneSystem::Destroy(root);
		SceneSystem::EndFrame();
	}

	EditorSelection::SelectAsset(modelGuid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {700, 1800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	UI::ElementHandle tab = UITest::FindByText(context, root, "Rig");
	ASSERT_TRUE(tab.IsValid());
	UITest::ClickElement(context, tab);
	Rebuild(context, root);

	ASSERT_TRUE(UITest::FindByTextContaining(context, root, "get a SceneObject").IsValid());

	UI::ElementHandle box = UITest::FindByClass(context, root, StringHash("slt-checkbox"), 0);
	ASSERT_TRUE(box.IsValid());
	UI::ElementHandle apply = UITest::FindByText(context, root, "Apply");
	ASSERT_TRUE(apply.IsValid());

	UITest::ClickElement(context, box);
	UITest::ClickElement(context, apply);
	Rebuild(context, root);

	{
		Handle<SceneObject> instance = SceneSystem::Instantiate(modelGuid);
		ASSERT_TRUE(instance.IsValid());
		Skeleton* skeleton = instance->GetComponent<Skeleton>().Get();
		ASSERT_NE(skeleton, nullptr);
		EXPECT_EQ(skeleton->GetExposedBoneCount(), exposedBefore - 1);
		for (u32 slot = 0; slot < skeleton->GetExposedBoneCount(); ++slot) {
			EXPECT_TRUE(skeleton->GetExposedObject(slot).IsValid());
		}
		SceneSystem::Destroy(instance);
		SceneSystem::EndFrame();
	}
}

TEST_F(AnimationTabsFixture, TypeChangeKeepsClipRanges) {
	if (!StageFixture("Fox.glb")) {
		GTEST_SKIP();
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);
	ASSERT_EQ(ClipKeysOf(modelGuid).size(), 3u);

	EditorSelection::SelectAsset(modelGuid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {700, 1800};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	{
		UI::ElementHandle tab = UITest::FindByText(context, root, "Animation");
		ASSERT_TRUE(tab.IsValid());
		UITest::ClickElement(context, tab);
		Rebuild(context, root);

		UI::ElementHandle add = UITest::FindByText(context, root, "+");
		ASSERT_TRUE(add.IsValid());
		UI::ElementHandle apply = UITest::FindByText(context, root, "Apply");
		ASSERT_TRUE(apply.IsValid());
		UITest::ClickElement(context, add);
		UITest::ClickElement(context, apply);
		Rebuild(context, root);
	}

	ASSERT_TRUE(ClipGuidFor(modelGuid, "Clips/Clip0").IsValid());
	{
		const Vector<str> keys = ClipKeysOf(modelGuid);
		ASSERT_EQ(keys.size(), 4u);
	}

	UI::ElementHandle rigTab = UITest::FindByText(context, root, "Rig");
	ASSERT_TRUE(rigTab.IsValid());
	UITest::ClickElement(context, rigTab);
	Rebuild(context, root);

	auto pickAnimationType = [&](u32 row, std::string_view expected) {
		UI::ElementHandle dropdown = UITest::FindByClass(context, root, StringHash("slt-dropdown"), 0);
		ASSERT_TRUE(dropdown.IsValid());
		UITest::ClickElement(context, dropdown);
		ASSERT_EQ(context.GetOpenPopupCount(), 1u);
		UI::ElementHandle popup = context.GetOpenPopup(0);
		UI::ElementHandle item = UITest::NthChildOf(context, popup, row);
		ASSERT_TRUE(item.IsValid()) << row;
		EXPECT_TRUE(UITest::FindByText(context, item, expected).IsValid() || context.GetTextString(item) == expected) << "row " << row << ' ' << expected << "'";
		UITest::ClickElement(context, item);
		Rebuild(context, root);
	};

	pickAnimationType(1, "None");

	{
		Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Fox.glb")));
		const str meta = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
		ASSERT_FALSE(meta.empty());
		EXPECT_NE(meta.find("\"None\""), str::npos);
		EXPECT_NE(meta.find("\"Clip0\""), str::npos);
	}

	pickAnimationType(0, "Generic");

	EditorSelection::Clear();
	Rebuild(context, root);
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Fox.glb")));

	ASSERT_TRUE(ClipGuidFor(modelGuid, "Clips/Clip0").IsValid());
	AssetDatabase::CollectGarbage();
	Asset* authored = AssetDatabase::LoadAssetByGuid(ClipGuidFor(modelGuid, "Clips/Clip0"));
	EXPECT_NE(authored, nullptr);
	if (authored) {
		AssetDatabase::UnloadAsset(authored);
	}

	Asset* take = AssetDatabase::LoadAssetByGuid(ClipGuidFor(modelGuid, "Clips/Survey"));
	EXPECT_EQ(take, nullptr);
	if (take) {
		AssetDatabase::UnloadAsset(take);
	}
}

TEST_F(AnimationTabsFixture, SampleBindPoseWritesStanceOnly) {
	if (!StageFixture("HumanoidRig.gltf")) {
		GTEST_SKIP();
	}
	const Path model("project://HumanoidRig.gltf");
	Asset* asset = AssetDatabase::LoadAsset(model);
	ASSERT_NE(asset, nullptr);
	const Guid guid = asset->assetId;
	AssetDatabase::UnloadAsset(asset);

	ASSERT_TRUE(WriteAnimationBlock(
		"project://HumanoidRig.gltf",
		R"("animation":{"animationType":"Generic","clips":[{"name":"Rise","start":0.0,"end":0.5,"loop":"None"}]})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	EditorSelection::SelectAsset(guid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {700, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	UI::ElementHandle rigTab = UITest::FindByText(context, root, "Rig");
	ASSERT_TRUE(rigTab.IsValid());
	UITest::ClickElement(context, rigTab);
	Rebuild(context, root);

	ASSERT_TRUE(UITest::FindByText(context, root, "Rig Pose").IsValid());
	UI::ElementHandle dropdown = UITest::FindByClass(context, root, StringHash("slt-dropdown"), 1);
	ASSERT_TRUE(dropdown.IsValid());
	UITest::ClickElement(context, dropdown);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UI::ElementHandle item = UITest::NthChildOf(context, context.GetOpenPopup(0), 1);
	ASSERT_TRUE(item.IsValid());
	EXPECT_TRUE(UITest::FindByText(context, item, "Sample Bind-pose").IsValid() ||
				context.GetTextString(item) == "Sample Bind-pose");
	UITest::ClickElement(context, item);
	Rebuild(context, root);

	{
		Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(model));
		const str meta = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
		ASSERT_FALSE(meta.empty());
		EXPECT_NE(meta.find("\"BindPose\""), str::npos);
		EXPECT_NE(meta.find("\"Rise\""), str::npos);
	}

	UI::ElementHandle again = UITest::FindByClass(context, root, StringHash("slt-dropdown"), 1);
	ASSERT_TRUE(again.IsValid());
	UITest::ClickElement(context, again);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UI::ElementHandle back = UITest::NthChildOf(context, context.GetOpenPopup(0), 0);
	ASSERT_TRUE(back.IsValid());
	UITest::ClickElement(context, back);
	Rebuild(context, root);
	{
		Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(model));
		const str meta = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
		EXPECT_NE(meta.find("\"FromModel\""), str::npos);
	}
}

TEST_F(AnimationTabsFixture, CopyFromModelShowsSourceRow) {
	if (!StageFixture("HumanoidRig.gltf")) {
		GTEST_SKIP();
	}
	ASSERT_TRUE(StageFixtureAs("HumanoidRig.gltf", "Assets/Twin.gltf"));

	const Path model("project://HumanoidRig.gltf");
	Asset* asset = AssetDatabase::LoadAsset(model);
	ASSERT_NE(asset, nullptr);
	const Guid guid = asset->assetId;
	AssetDatabase::UnloadAsset(asset);
	{
		Asset* twin = AssetDatabase::LoadAsset(Path("project://Assets/Twin.gltf"));
		ASSERT_NE(twin, nullptr);
		AssetDatabase::UnloadAsset(twin);
	}

	EditorSelection::SelectAsset(guid);
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {700, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root;
	BuildInspector(context, root);

	UI::ElementHandle rigTab = UITest::FindByText(context, root, "Rig");
	ASSERT_TRUE(rigTab.IsValid());
	UITest::ClickElement(context, rigTab);
	Rebuild(context, root);

	ASSERT_FALSE(UITest::FindByText(context, root, "Source").IsValid());

	UI::ElementHandle dropdown = UITest::FindByClass(context, root, StringHash("slt-dropdown"), 1);
	ASSERT_TRUE(dropdown.IsValid());
	UITest::ClickElement(context, dropdown);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UI::ElementHandle item = UITest::NthChildOf(context, context.GetOpenPopup(0), 2);
	ASSERT_TRUE(item.IsValid());
	EXPECT_TRUE(UITest::FindByText(context, item, "Copy From Model").IsValid() ||
				context.GetTextString(item) == "Copy From Model");
	UITest::ClickElement(context, item);
	Rebuild(context, root);

	{
		Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(model));
		const str meta = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
		EXPECT_NE(meta.find("\"CopyFrom\""), str::npos);
	}
	ASSERT_TRUE(UITest::FindByText(context, root, "Source").IsValid());

	UI::ElementHandle sourceDropdown = UITest::FindByClass(context, root, StringHash("slt-dropdown"), 2);
	ASSERT_TRUE(sourceDropdown.IsValid());
	UITest::ClickElement(context, sourceDropdown);
	ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	UI::ElementHandle twinItem = UITest::NthChildOf(context, context.GetOpenPopup(0), 1);
	ASSERT_TRUE(twinItem.IsValid());
	UITest::ClickElement(context, twinItem);
	Rebuild(context, root);

	const Guid twinGuid = AssetDatabase::LookupGuid(Path("project://Assets/Twin.gltf"));
	ASSERT_TRUE(twinGuid.IsValid());
	{
		Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(model));
		const str meta = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
		EXPECT_NE(meta.find(twinGuid.ToString()), str::npos);
	}
}

TEST_F(AnimationTabsFixture, RegistersBadgeAndGroupForBoth) {
	const TypeInfo* animator = TypeRegistry::FindType(StringHash("Animator"));
	const TypeInfo* controller = TypeRegistry::FindType(StringHash("AnimatorStateMachine"));
	ASSERT_NE(animator, nullptr);
	ASSERT_NE(controller, nullptr);

	EXPECT_EQ(EditorIcons::ComponentIcons::For(animator), "engine://Icons/Components/Animator.svg");
	EXPECT_EQ(EditorIcons::ComponentIcons::For(controller),
			  "engine://Icons/Components/AnimatorController.svg");
	EXPECT_EQ(ComponentMenu::CategoryFor(animator), "Animation");
	EXPECT_EQ(ComponentMenu::CategoryFor(controller), "Animation");

	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("AnimationClip")).icon.glyph,
			  EditorIcons::Glyph::AssetAnimationClip.glyph);
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("AnimatorController")).icon.glyph,
			  EditorIcons::Glyph::AssetAnimatorController.glyph);

	EXPECT_GT(EditorExtensions::Unload(_moduleBase), 0u);
	EXPECT_EQ(EditorIcons::ComponentIcons::For(controller), EditorIcons::Mark::Behavior);
	EXPECT_TRUE(ComponentMenu::CategoryFor(controller).empty());
	EXPECT_EQ(ComponentMenu::CategoryFor(TypeRegistry::FindType(StringHash("Camera"))), "Rendering");
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("AnimationClip")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);
}

#endif
