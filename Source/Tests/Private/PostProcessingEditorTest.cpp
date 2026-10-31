#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	constexpr const char* PACKAGE_TARGET = "PostProcessing";

	class PostProcessingEditorFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;
		const void* _moduleBase = nullptr;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 unique = counter.fetch_add(1);
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_postprocessing_editor_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			EditorGizmos::Initialize();

			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			_moduleBase = ModuleSystem::GetNativeHandle(_module);
			ASSERT_NE(_moduleBase, nullptr);

			ASSERT_TRUE(EditorExtensions::Load(_moduleBase));
		}

		void TearDown() override {
			if (_moduleBase) {
				EditorExtensions::Unload(_moduleBase);
			}
			EditorGizmos::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}
	};

} // anonymous namespace

TEST_F(PostProcessingEditorFixture, PackageRegistersBadgeAndGroupForComponent) {
	const TypeInfo* volume = TypeRegistry::FindType(StringHash("PostProcessVolume"));
	ASSERT_NE(volume, nullptr);

	EXPECT_EQ(EditorIcons::ComponentIcons::For(volume), "engine://Icons/Components/PostProcessVolume.svg");
	EXPECT_EQ(ComponentMenu::CategoryFor(volume), "Rendering");

	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("PostProcessProfile")).icon.glyph,
			  EditorIcons::Glyph::Effects.glyph);

	EXPECT_GT(EditorExtensions::Unload(_moduleBase), 0u);
	_moduleBase = nullptr;
	EXPECT_EQ(EditorIcons::ComponentIcons::For(volume), EditorIcons::Mark::Component);
	EXPECT_TRUE(ComponentMenu::CategoryFor(volume).empty());
	EXPECT_EQ(EditorPreviews::BadgeForAssetType(StringHash("PostProcessProfile")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);
}

#endif
