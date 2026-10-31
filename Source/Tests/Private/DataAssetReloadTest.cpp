#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/StringUtil.h"

#include "TestHelperProcess.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>

using namespace Shalltear;

namespace {

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";

	constexpr const char* PROBE_TYPE = "HotReloadDataAsset";
	constexpr const char* PROBE_PATH = "project://ProbeData.hrdata";

} // anonymous namespace

namespace Shalltear::TestHelpers {

#ifdef _WIN32

	namespace {

		std::atomic<bool> _insideReload{false};

		LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* info) {
			const bool breakpoint = info && info->ExceptionRecord &&
									info->ExceptionRecord->ExceptionCode == static_cast<DWORD>(EXCEPTION_BREAKPOINT);
			UINT code = static_cast<UINT>(-1);
			if (breakpoint) {
				code = _insideReload.load() ? DataAssetSweepAborted : DataAssetAbortedInSetup;
			}
			::TerminateProcess(::GetCurrentProcess(), code);
			return EXCEPTION_EXECUTE_HANDLER;
		}

		const void* VtableOf(const Object* object) {
			return *reinterpret_cast<void* const*>(object);
		}

		bool AddressIsInImage(const void* address, const void* moduleBase) {
			MEMORY_BASIC_INFORMATION region{};
			if (::VirtualQuery(address, &region, sizeof(region)) == 0) {
				return false;
			}
			return region.State == MEM_COMMIT && region.AllocationBase == moduleBase;
		}

		[[noreturn]] void Finish(int code) {
			std::_Exit(code);
		}

	} // anonymous namespace

	int RunDataAssetReloadProbe(const char* projectRoot) {
		if (!projectRoot || *projectRoot == '\0') {
			Finish(DataAssetNoProjectRoot);
		}

		::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
		::SetUnhandledExceptionFilter(&OnUnhandled);

		FileSystem::Mount("project", Path(str(projectRoot)));
		TypeRegistry::InitializePrimitives();
		SceneSystem::Initialize();
		AssetDatabase::Initialize();
		ModuleSystem::Initialize();

		ModuleHandle module = ModuleSystem::Load(Path(V1_DLL));
		if (!ModuleSystem::IsLoaded(module)) {
			Finish(DataAssetModuleLoadFailed);
		}
		const void* v1Base = ModuleSystem::GetNativeHandle(module);
		if (!TypeRegistry::FindType(StringHash(PROBE_TYPE))) {
			Finish(DataAssetTypeMissing);
		}

		if (!AssetDatabase::CreateAsset(Path(PROBE_PATH), StringHash(PROBE_TYPE)).IsValid()) {
			Finish(DataAssetCreateFailed);
		}
		Asset* live = AssetDatabase::LoadAsset(Path(PROBE_PATH));
		if (!live) {
			Finish(DataAssetCreateFailed);
		}

		if (!AddressIsInImage(VtableOf(live), v1Base)) {
			Finish(DataAssetNotModuleOwned);
		}

		struct Census {
			const void* image;
			u32 count;
		} census{v1Base, 0};
		ObjectRegistry::ForEachLiveObject(
			+[](Object* object, u64, void* user) {
				auto* c = static_cast<Census*>(user);
				if (AddressIsInImage(VtableOf(object), c->image)) {
					++c->count;
				}
			},
			&census);
		if (census.count != 1) {
			Finish(DataAssetNotTheOnlyOne);
		}

		const StringHash typeBefore = live->GetAssetTypeId();
		const Asset* addressBefore = live;
		const void* vptrBefore = VtableOf(live);
		Handle<Asset> handle(live);
		const u64 idBefore = live->GetInstanceId();

		const TypeInfo* probeType = TypeRegistry::FindType(StringHash(PROBE_TYPE));
		Variant edited{static_cast<i32>(4242)};
		if (!SetField(live, probeType, StringHash("damage"), &edited)) {
			Finish(DataAssetCreateFailed);
		}

		Path v2{V2_DLL};
		_insideReload.store(true);
		const bool reloaded = ModuleSystem::Reload(module, &v2);
		_insideReload.store(false);
		if (!reloaded) {
			Finish(DataAssetReloadRefused);
		}

		const void* v2Base = ModuleSystem::GetNativeHandle(module);
		Asset* after = handle.Get();
		std::printf("v1Base=%p v2Base=%p before=%p vptrBefore=%p after=%p vptrAfter=%p\n",
					v1Base, v2Base, static_cast<const void*>(addressBefore), vptrBefore,
					static_cast<const void*>(after), after ? VtableOf(after) : nullptr);
		std::fflush(stdout);
		if (!after) {
			Finish(DataAssetHandleLost);
		}

		if (after == addressBefore && VtableOf(after) == vptrBefore) {
			Finish(AddressIsInImage(vptrBefore, v2Base) ? DataAssetVtableAliased : DataAssetVtableDangled);
		}

		if (after->GetAssetTypeId() != typeBefore) {
			Finish(DataAssetWrongType);
		}
		if (after->GetInstanceId() != idBefore) {
			Finish(DataAssetIdChanged);
		}

		const TypeInfo* afterType = TypeRegistry::FindType(StringHash(PROBE_TYPE));
		Variant damage;
		if (!afterType || !GetField(after, afterType, StringHash("damage"), &damage)) {
			Finish(DataAssetValueLost);
		}
		const i32* value = damage.TryGet<i32>();
		if (!value || *value != 4242) {
			Finish(DataAssetValueLost);
		}

		Variant magic;
		if (!InvokeMethod(after, afterType, StringHash("GetAssetMagic"), nullptr, 0, &magic)) {
			Finish(DataAssetNewCodeAbsent);
		}
		const i32* magicValue = magic.TryGet<i32>();
		if (!magicValue || *magicValue != 2) {
			Finish(DataAssetNewCodeAbsent);
		}
		Finish(DataAssetSurvived);
	}

#endif

} // namespace Shalltear::TestHelpers

namespace {

#ifdef _WIN32

	bool RunProbe(const std::filesystem::path& projectRoot, DWORD& outExitCode, str& outWhy) {
		wchar_t exePath[MAX_PATH] = {};
		if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
			outWhy = "could not resolve this executable's path";
			return false;
		}

		std::wstring commandLine = L"\"";
		commandLine += exePath;
		commandLine += L"\" ";
		commandLine += StringUtil::UTF8ToWide(Shalltear::TestHelpers::DATA_ASSET_RELOAD_FLAG);
		commandLine += L" \"";
		commandLine += StringUtil::UTF8ToWide(str(projectRoot.generic_string()));
		commandLine += L"\"";

		HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
		if (!job) {
			outWhy = "could not create the job object";
			return false;
		}
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

		PROCESS_INFORMATION process{};
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
							  CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
			::CloseHandle(job);
			outWhy = "could not spawn the helper process";
			return false;
		}
		::AssignProcessToJobObject(job, process.hProcess);
		::ResumeThread(process.hThread);

		DWORD wait = ::WaitForSingleObject(process.hProcess, 60000);
		outExitCode = static_cast<DWORD>(-1);
		if (wait == WAIT_OBJECT_0) {
			::GetExitCodeProcess(process.hProcess, &outExitCode);
		} else {
			::TerminateProcess(process.hProcess, 1);
			outWhy = "the helper never exited";
		}
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
		::CloseHandle(job);
		return wait == WAIT_OBJECT_0;
	}

	class DataAssetReloadFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_dataasset_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}
	};

#endif

} // anonymous namespace

#ifdef _WIN32

TEST_F(DataAssetReloadFixture, ModuleAssetSurvivesCodeReload) {
	DWORD exitCode = 0;
	str why;
	ASSERT_TRUE(RunProbe(_root, exitCode, why)) << why;

	ASSERT_LT(exitCode, static_cast<DWORD>(Shalltear::TestHelpers::DataAssetModuleLoadFailed)) << exitCode;

	EXPECT_EQ(exitCode, static_cast<DWORD>(Shalltear::TestHelpers::DataAssetSurvived));
}

#endif

TEST(DataAssetModuleTest, ModuleDefinesCreatableType) {
	TypeRegistry::InitializePrimitives();
	SceneSystem::Initialize();
	ModuleSystem::Initialize();

	EXPECT_EQ(TypeRegistry::FindType(StringHash(PROBE_TYPE)), nullptr);

	ModuleHandle module = ModuleSystem::Load(Path(V1_DLL));
	ASSERT_TRUE(ModuleSystem::IsLoaded(module));

	const TypeInfo* type = TypeRegistry::FindType(StringHash(PROBE_TYPE));
	ASSERT_NE(type, nullptr);
	EXPECT_EQ(type->ownerModule, ModuleSystem::GetNativeHandle(module));

	const TypeInfo* assetType = TypeRegistry::FindType(StringHash("Asset"));
	ASSERT_NE(assetType, nullptr);
	EXPECT_TRUE(TypeRegistry::IsDerivedFrom(type, assetType));

	IAssetImporter* importer = AssetImporterRegistry::Find(StringHash(PROBE_TYPE));
	ASSERT_NE(importer, nullptr);
	EXPECT_TRUE(importer->CanCreateAsset());
	EXPECT_EQ(importer->GetCreateMenuLabel(), "Hot Reload Data");

	ASSERT_TRUE(ModuleSystem::Unload(module));
	EXPECT_EQ(TypeRegistry::FindType(StringHash(PROBE_TYPE)), nullptr);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash(PROBE_TYPE)), nullptr);

	SceneSystem::Terminate();
	ModuleSystem::Terminate();
}
