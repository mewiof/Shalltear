#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "TestHelperProcess.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/UnresolvedComponent.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	str NameOf(const TypeInfo* type) {
		return type && type->displayName ? str(type->displayName) : str("<unnamed type>");
	}

	bool IsADeliberateFloor(const TypeInfo* type) {
		return type == Component::GetReflectType() || type == Behavior::GetReflectType() ||
			   type == UnresolvedComponent::GetReflectType();
	}

	bool DerivesFromComponent(const TypeInfo* type) {
		for (const TypeInfo* walk = type; walk != nullptr; walk = walk->parent) {
			if (walk == Component::GetReflectType()) {
				return true;
			}
		}
		return false;
	}

	struct ComponentSweep {
		const void* coreImage = nullptr;
		u32 visited = 0;
		Vector<str> wearingTheCog{GetMemoryResource(MemTag::Editor)};
		Vector<str> withNoGroup{GetMemoryResource(MemTag::Editor)};
	};

	void VisitComponentType(const TypeInfo* type, void* user) {
		auto& sweep = *static_cast<ComponentSweep*>(user);
		if (!type || type->ownerModule != sweep.coreImage || !DerivesFromComponent(type) ||
			IsADeliberateFloor(type)) {
			return;
		}
		++sweep.visited;
		if (EditorIcons::ComponentIcons::For(type) == EditorIcons::Mark::Component) {
			sweep.wearingTheCog.push_back(NameOf(type));
		}
		if (ComponentMenu::CategoryFor(type).empty()) {
			sweep.withNoGroup.push_back(NameOf(type));
		}
	}

	str Join(const Vector<str>& names) {
		str out;
		for (const str& name : names) {
			if (!out.empty()) {
				out += ", ";
			}
			out += name;
		}
		return out;
	}

} // anonymous namespace

TEST(EditorIconCoverageTest, CoreComponentsHaveBadgeAndGroup) {
	EditorIcons::ComponentIcons::RegisterBuiltIns();
	ComponentMenu::RegisterBuiltIns();

	ComponentSweep sweep;
	sweep.coreImage = Component::GetReflectType()->ownerModule;
	ASSERT_NE(sweep.coreImage, nullptr);

	TypeRegistry::ForEachType(&VisitComponentType, &sweep);

	ASSERT_GT(sweep.visited, 5u);

	EXPECT_TRUE(sweep.wearingTheCog.empty());
	EXPECT_TRUE(sweep.withNoGroup.empty());
}

TEST(EditorIconCoverageTest, EveryImportableAssetTypeCarriesBadge) {
	static std::atomic<u64> counter{0};
	const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
	std::filesystem::path root = std::filesystem::temp_directory_path() /
								 ("shalltear_icon_coverage_" + std::to_string(stamp) + "_" +
								  std::to_string(counter.fetch_add(1)));
	std::filesystem::create_directories(root / "Assets");
	FileSystem::Mount("project", Path(root.generic_string()));

	TypeRegistry::InitializePrimitives();
	AssetDatabase::Initialize();
	EditorPreviews::AssetBadges::RegisterBuiltIns();
	TestHelpers::RestoreNodeShader();

	Vector<str> generic{GetMemoryResource(MemTag::Editor)};
	u32 visited = 0;
	AssetImporterRegistry::ForEach(
		[&](StringHash typeId, IAssetImporter*, std::string_view primaryExtension) {
			++visited;
			if (EditorPreviews::BadgeForAssetType(typeId).icon.glyph ==
				EditorIcons::Glyph::AssetGeneric.glyph) {
				generic.push_back(str(primaryExtension));
			}
		});

	EXPECT_GT(visited, 10u);
	str names;
	for (const str& name : generic) {
		names += " " + name;
	}
	EXPECT_TRUE(generic.empty()) << names;

	EXPECT_NE(EditorPreviews::BadgeForAssetType(StringHash("SkeletonAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);
	EXPECT_NE(EditorPreviews::BadgeForAssetType(StringHash("SkinAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);

	AssetDatabase::Terminate();
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
}

#endif
