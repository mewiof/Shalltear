#include "TestsPCH.h"

#include "TestPlatform.h"
#include "TestHelperProcess.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/StringUtil.h"

#include <array>

using namespace Shalltear;

namespace {

	constexpr std::array<const char*, 8> CoreBaseTypes{
		"Object",
		"Asset",
		"Component",
		"Behavior",
		"SceneObject",
		"Scene",
		"UnresolvedComponent",
		"RenderPass"};

	constexpr const char* SAMPLE_MODULE_DLL = "ShalltearSampleModule" SLT_TEST_DLL_SUFFIX;

	class DrainOrderProbe : public Object {
	public:
		SLT_REFLECT_CLASS(DrainOrderProbe)
		SLT_REFLECT_PARENT(Object)
		SLT_END_REFLECT()
	};

} // anonymous namespace

SLT_REGISTER_TYPE(DrainOrderProbe)

TEST(CoreTypeOwnershipTest, ReflectionStartupRegistersCoresOwnBaseTypes) {
	TypeRegistry::InitializePrimitives();

	for (const char* name : CoreBaseTypes) {
		EXPECT_NE(TypeRegistry::FindType(StringHash(name)), nullptr);
	}
}

TEST(CoreTypeOwnershipTest, CoresTypesAllReportSameOwningImage) {
	TypeRegistry::InitializePrimitives();

	const TypeInfo* root = TypeRegistry::FindType(StringHash("Object"));
	ASSERT_NE(root, nullptr);
	const void* coreImage = root->ownerModule;
	ASSERT_NE(coreImage, nullptr);

	for (const char* name : CoreBaseTypes) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(name));
		ASSERT_NE(type, nullptr) << name;
		EXPECT_EQ(type->ownerModule, coreImage);
	}

#ifdef SHALLTEAR_SHARED
	EXPECT_NE(coreImage, static_cast<const void*>(ModuleSystem::GetExecutableImage()));
#endif
}

TEST(CoreTypeOwnershipTest, UnloadKeepsCoreTypes) {
	TypeRegistry::InitializePrimitives();

	std::array<const void*, CoreBaseTypes.size()> ownersBefore{};
	for (size_t i = 0; i < CoreBaseTypes.size(); ++i) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(CoreBaseTypes[i]));
		ASSERT_NE(type, nullptr) << CoreBaseTypes[i];
		ownersBefore[i] = type->ownerModule;
	}

	ModuleSystem::Initialize();
	ModuleHandle handle = ModuleSystem::Load(Path(SAMPLE_MODULE_DLL));
	if (!ModuleSystem::IsLoaded(handle)) {
		ModuleSystem::Terminate();
		GTEST_SKIP() << SAMPLE_MODULE_DLL << " not found";
	}

	ASSERT_NE(TypeRegistry::FindType(StringHash("SampleReflected")), nullptr);
	ASSERT_NE(TypeRegistry::FindType(StringHash("SamplePass")), nullptr);

	ASSERT_TRUE(ModuleSystem::Unload(handle));
	EXPECT_EQ(TypeRegistry::FindType(StringHash("SampleReflected")), nullptr);

	for (size_t i = 0; i < CoreBaseTypes.size(); ++i) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(CoreBaseTypes[i]));
		ASSERT_NE(type, nullptr) << CoreBaseTypes[i];
		EXPECT_EQ(type->ownerModule, ownersBefore[i]) << CoreBaseTypes[i];
	}

	ModuleSystem::Terminate();
}

namespace Shalltear::TestHelpers {

	int RunReflectionStartupProbe() {
		TypeRegistry::InitializePrimitives();

		if (TypeRegistry::FindType(StringHash("i32")) == nullptr) {
			return ReflectionStartupNoPrimitives;
		}
		for (size_t i = 0; i < CoreBaseTypes.size(); ++i) {
			if (TypeRegistry::FindType(StringHash(CoreBaseTypes[i])) == nullptr) {
				return ReflectionStartupFirstMissing + static_cast<int>(i);
			}
		}
		return ReflectionStartupOk;
	}

} // namespace Shalltear::TestHelpers

TEST(CoreTypeOwnershipTest, ReflectionStepRegistersCoreTypes) {
	const str commandLine = "\"" + FileSystem::GetExecutablePath().ToString() + "\" " + TestHelpers::REFLECTION_STARTUP_FLAG;
	const TestPlatform::ProcessResult result = TestPlatform::RunProcess(commandLine, 30000);
	if (!result.started) {
		GTEST_SKIP() << "couldn't spawn helper process";
	}
	ASSERT_TRUE(result.exited);
	ASSERT_NE(result.exitCode, TestHelpers::ReflectionStartupNoPrimitives);

	const size_t index = static_cast<size_t>(result.exitCode - TestHelpers::ReflectionStartupFirstMissing);
	const char* missing = "<none>";
	if (result.exitCode >= TestHelpers::ReflectionStartupFirstMissing && index < CoreBaseTypes.size()) {
		missing = CoreBaseTypes[index];
	}
	EXPECT_EQ(result.exitCode, TestHelpers::ReflectionStartupOk) << missing << " unregistered";
}

#ifdef SHALLTEAR_SHARED
TEST(CoreTypeOwnershipTest, DrainReachesCoresNodesBeforeThisImages) {
	TypeRegistry::InitializePrimitives();

	const TypeInfo* fromCore = TypeRegistry::FindType(StringHash("Camera"));
	const TypeInfo* fromHere = TypeRegistry::FindType(StringHash("DrainOrderProbe"));
	ASSERT_NE(fromCore, nullptr);
	ASSERT_NE(fromHere, nullptr);

	EXPECT_LT(fromCore->classId, fromHere->classId);
	EXPECT_EQ(fromCore->ownerModule, Object::GetReflectType()->ownerModule);
}
#endif
