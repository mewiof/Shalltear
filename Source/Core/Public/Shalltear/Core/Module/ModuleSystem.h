#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct ModuleHandle {
		u32 index = 0;
		u32 generation = 0;

		bool operator==(const ModuleHandle&) const = default;
	};

	enum class ModuleLoadPolicy : u8 {
		CacheCopy,
		InPlace,
	};

	class SHALLTEAR_CORE_API ModuleSystem {
	public:
		static void Initialize();
		static void Terminate();
		static bool IsInitialized();

		static ModuleHandle Load(const Path& binaryPath,
								 ModuleLoadPolicy policy = ModuleLoadPolicy::CacheCopy);

		static bool Unload(ModuleHandle handle);

		static bool Reload(ModuleHandle handle, const Path* newBinaryPath = nullptr);

		static bool IsLoaded(ModuleHandle handle);
		static u32 GetLoadedCount();
		static str GetModuleName(ModuleHandle handle);
		static Path GetModuleBinaryPath(ModuleHandle handle);
		static void* GetNativeHandle(ModuleHandle handle);
	};

} // namespace Shalltear
