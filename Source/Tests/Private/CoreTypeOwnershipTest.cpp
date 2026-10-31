#include "TestsPCH.h"

#include "TestHelperProcess.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/StringUtil.h"

#include <array>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace Shalltear;

namespace {

	constexpr std::array<const char*, 8> CORE_BASE_TYPES{
		"Object",
		"Asset",
		"Component",
		"Behavior",
		"SceneObject",
		"Scene",
		"UnresolvedComponent",
		"RenderPass"};

	constexpr const char* SAMPLE_MODULE_DLL = "ShalltearSampleModule.dll";

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

	for (const char* name : CORE_BASE_TYPES) {
		EXPECT_NE(TypeRegistry::FindType(StringHash(name)), nullptr);
	}
}

TEST(CoreTypeOwnershipTest, CoresTypesAllReportSameOwningImage) {
	TypeRegistry::InitializePrimitives();

	const TypeInfo* root = TypeRegistry::FindType(StringHash("Object"));
	ASSERT_NE(root, nullptr);
	const void* coreImage = root->ownerModule;
	ASSERT_NE(coreImage, nullptr);

	for (const char* name : CORE_BASE_TYPES) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(name));
		ASSERT_NE(type, nullptr) << name;
		EXPECT_EQ(type->ownerModule, coreImage);
	}

#ifdef SHALLTEAR_SHARED
	EXPECT_NE(coreImage, static_cast<const void*>(::GetModuleHandleW(nullptr)));
#endif
}

TEST(CoreTypeOwnershipTest, UnloadKeepsCoreTypes) {
	TypeRegistry::InitializePrimitives();

	std::array<const void*, CORE_BASE_TYPES.size()> ownersBefore{};
	for (size_t i = 0; i < CORE_BASE_TYPES.size(); ++i) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(CORE_BASE_TYPES[i]));
		ASSERT_NE(type, nullptr) << CORE_BASE_TYPES[i];
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

	for (size_t i = 0; i < CORE_BASE_TYPES.size(); ++i) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(CORE_BASE_TYPES[i]));
		ASSERT_NE(type, nullptr) << CORE_BASE_TYPES[i];
		EXPECT_EQ(type->ownerModule, ownersBefore[i]) << CORE_BASE_TYPES[i];
	}

	ModuleSystem::Terminate();
}

namespace Shalltear::TestHelpers {

	int RunReflectionStartupProbe() {
		TypeRegistry::InitializePrimitives();

		if (TypeRegistry::FindType(StringHash("i32")) == nullptr) {
			return ReflectionStartupNoPrimitives;
		}
		for (size_t i = 0; i < CORE_BASE_TYPES.size(); ++i) {
			if (TypeRegistry::FindType(StringHash(CORE_BASE_TYPES[i])) == nullptr) {
				return ReflectionStartupFirstMissing + static_cast<int>(i);
			}
		}
		return ReflectionStartupOk;
	}

} // namespace Shalltear::TestHelpers

TEST(CoreTypeOwnershipTest, ReflectionStepRegistersCoreTypes) {
	wchar_t exePath[MAX_PATH] = {};
	ASSERT_GT(::GetModuleFileNameW(nullptr, exePath, MAX_PATH), 0u);
	std::wstring commandLine = L"\"";
	commandLine += exePath;
	commandLine += L"\" ";
	commandLine += StringUtil::UTF8ToWide(TestHelpers::REFLECTION_STARTUP_FLAG);

	HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
	ASSERT_NE(job, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
						  CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
		::CloseHandle(job);
		GTEST_SKIP() << "couldn't spawn helper process";
	}
	::AssignProcessToJobObject(job, process.hProcess);
	::ResumeThread(process.hThread);

	DWORD wait = ::WaitForSingleObject(process.hProcess, 30000);
	DWORD exitCode = static_cast<DWORD>(-1);
	if (wait == WAIT_OBJECT_0) {
		::GetExitCodeProcess(process.hProcess, &exitCode);
	} else {
		::TerminateProcess(process.hProcess, 1);
	}
	::CloseHandle(process.hThread);
	::CloseHandle(process.hProcess);
	::CloseHandle(job);

	ASSERT_EQ(wait, static_cast<DWORD>(WAIT_OBJECT_0));
	ASSERT_NE(exitCode, static_cast<DWORD>(TestHelpers::ReflectionStartupNoPrimitives));

	const DWORD index = exitCode - TestHelpers::ReflectionStartupFirstMissing;
	const char* missing = "<none>";
	if (exitCode >= TestHelpers::ReflectionStartupFirstMissing && index < CORE_BASE_TYPES.size()) {
		missing = CORE_BASE_TYPES[index];
	}
	EXPECT_EQ(exitCode, static_cast<DWORD>(TestHelpers::ReflectionStartupOk)) << missing << " unregistered";
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
