#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/StringHash.h"

#include "StaticLinkAnchors.h"

#include <array>
#include <string>
#include <vector>

using namespace Shalltear;

namespace {

#define SLT_ANCHOR_NAME(TypeName) #TypeName,
	constexpr std::array ROSTER{
		SLT_SCENE_SYSTEM_ANCHORS(SLT_ANCHOR_NAME)
			SLT_ASSET_DATABASE_ANCHORS(SLT_ANCHOR_NAME)
				SLT_RENDER_SYSTEM_ANCHORS(SLT_ANCHOR_NAME)
					SLT_UI_SYSTEM_ANCHORS(SLT_ANCHOR_NAME)
						SLT_SETTINGS_ANCHORS(SLT_ANCHOR_NAME)};
#undef SLT_ANCHOR_NAME

	bool NeedsAnchoring(const TypeInfo* type) {
		return type->createFn != nullptr || FindTypeAttribute(type, StringHash("SettingsCategory")) != nullptr;
	}

	bool RosterNames(StringHash name) {
		for (const char* entry : ROSTER) {
			if (StringHash(entry) == name) {
				return true;
			}
		}
		return false;
	}

	const void* CoreImage() {
		const TypeInfo* root = TypeRegistry::FindType(StringHash("Object"));
		return root ? root->ownerModule : nullptr;
	}

	struct Sweep {
		const void* coreImage = nullptr;
		std::vector<std::string> unanchored;
		u32 factoryTypes = 0;
	};

} // anonymous namespace

TEST(StaticLinkAnchorTest, FactoryTypesInRoster) {
	TypeRegistry::InitializePrimitives();
	TypeRegistry::RegisterPendingTypes();

	Sweep sweep;
	sweep.coreImage = CoreImage();
	ASSERT_NE(sweep.coreImage, nullptr);

	TypeRegistry::ForEachType(
		[](const TypeInfo* type, void* user) {
			Sweep& s = *static_cast<Sweep*>(user);
			if (type->ownerModule != s.coreImage || !NeedsAnchoring(type)) {
				return;
			}
			++s.factoryTypes;
			if (!RosterNames(type->name)) {
				s.unanchored.emplace_back(type->displayName ? type->displayName : "<unnamed>");
			}
		},
		&sweep);

	ASSERT_GE(sweep.factoryTypes, ROSTER.size());

	std::string names;
	for (const std::string& name : sweep.unanchored) {
		names += names.empty() ? "" : ", ";
		names += name;
	}
	EXPECT_TRUE(sweep.unanchored.empty());
}

TEST(StaticLinkAnchorTest, RosterHasFactoryTypes) {
	TypeRegistry::InitializePrimitives();
	TypeRegistry::RegisterPendingTypes();

	const void* coreImage = CoreImage();
	ASSERT_NE(coreImage, nullptr);

	for (const char* entry : ROSTER) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(entry));
		ASSERT_NE(type, nullptr) << entry;
		EXPECT_EQ(type->ownerModule, coreImage) << entry;
		EXPECT_TRUE(NeedsAnchoring(type));
	}
}

#endif
