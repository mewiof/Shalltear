#include "TestsPCH.h"

#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/TypeNameOps.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class TnDepartingComponent : public Component {
	public:
		i32 charges = 0;

		SLT_REFLECT_COMPONENT(TnDepartingComponent, Component)
		SLT_REFLECT_FIELD(charges)
		SLT_END_REFLECT()

		TnDepartingComponent() = default;
	};

	const StringHash Departing{"TnDepartingComponent"};

	class TypeNameOpsTest : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_typenames_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
		}

		void TearDown() override {
			TypeRegistry::Unregister(Departing);
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static void RegisterDeparting() {
			TypeRegistry::Register(const_cast<TypeInfo*>(TnDepartingComponent::GetReflectType()));
		}
	};

} // anonymous namespace

TEST_F(TypeNameOpsTest, NameSurvivesTypeItBelongsTo) {
	RegisterDeparting();
	TypeNameOps::Refresh();
	EXPECT_EQ(TypeNameOps::Lookup(Departing), "TnDepartingComponent");

	TypeRegistry::Unregister(Departing);
	ASSERT_EQ(TypeRegistry::FindType(Departing), nullptr);
	TypeNameOps::Refresh();
	EXPECT_EQ(TypeNameOps::Lookup(Departing), "TnDepartingComponent");
	EXPECT_EQ(TypeNameOps::MissingTypeLabel(Departing), "TnDepartingComponent");
}

TEST_F(TypeNameOpsTest, NameOutlivesEditorSessionThatLearnedIt) {
	RegisterDeparting();
	TypeNameOps::Refresh();
	TypeRegistry::Unregister(Departing);

	FileSystem::Unmount("project");
	FileSystem::Mount("project", Path(_root.generic_string()));
	TypeNameOps::Refresh();

	EXPECT_EQ(TypeNameOps::Lookup(Departing), "TnDepartingComponent");
	EXPECT_TRUE(std::filesystem::exists(_root / "Library" / "Editor" / "TypeNames.json"));
}

TEST_F(TypeNameOpsTest, ProjectThatNeverSawTypeGetsHash) {
	TypeNameOps::Refresh();
	EXPECT_TRUE(TypeNameOps::Lookup(Departing).empty());
	EXPECT_EQ(TypeNameOps::MissingTypeLabel(Departing), std::format("{:#018x}", Departing.GetValue()));
}

TEST_F(TypeNameOpsTest, DifferentProjectDoesNotInheritNames) {
	RegisterDeparting();
	TypeNameOps::Refresh();
	ASSERT_EQ(TypeNameOps::Lookup(Departing), "TnDepartingComponent");
	TypeRegistry::Unregister(Departing);

	std::filesystem::path other = _root.parent_path() / (_root.filename().string() + "_other");
	std::filesystem::create_directories(other);
	FileSystem::Unmount("project");
	FileSystem::Mount("project", Path(other.generic_string()));
	TypeNameOps::Refresh();
	EXPECT_TRUE(TypeNameOps::Lookup(Departing).empty());

	FileSystem::Unmount("project");
	FileSystem::Mount("project", Path(_root.generic_string()));
	std::error_code ec;
	std::filesystem::remove_all(other, ec);
}
