#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	using HandleToGuidFn = bool (*)(u64 instanceId, Guid* outGuid, StringHash* outPointeeType);

	using GuidToHandleFn = u64 (*)(const Guid& guid, StringHash pointeeType);

	struct HandleResolverScope {
		bool (*toGuid)(void* user, u64 instanceId, Guid* outGuid, StringHash* outPointeeType) = nullptr;
		bool (*toHandle)(void* user, const Guid& guid, StringHash pointeeType, u64* outId) = nullptr;
		void* user = nullptr;
	};

	constexpr u64 SLT_LOCAL_SENTINEL = 0x534C544C4F43414CULL;
	constexpr u64 SLT_PREFAB_KEY_SENTINEL = 0x534C54504641424BULL;

	constexpr bool IsAssetGuid(const Guid& guid) noexcept {
		return guid.IsValid() && guid.high != SLT_LOCAL_SENTINEL && guid.high != SLT_PREFAB_KEY_SENTINEL;
	}

	using HandleObserverFn = void (*)(void* user, const Guid& guid, StringHash pointeeType);

	class SHALLTEAR_CORE_API HandleResolver {
	public:
		static void SetHooks(HandleToGuidFn toGuid, GuidToHandleFn toHandle) noexcept;

		static void ResetHooks() noexcept;

		static bool ToGuid(u64 instanceId, Guid* outGuid, StringHash* outPointeeType) noexcept;

		static u64 ToHandle(const Guid& guid, StringHash pointeeType) noexcept;

		static void PushScope(const HandleResolverScope& scope) noexcept;
		static void PopScope() noexcept;
		static u32 GetScopeDepth() noexcept;

		// 1 slot, only the cook installs it
		static void SetObserver(HandleObserverFn fn, void* user) noexcept;
	};

	class ScopedHandleResolver {
	public:
		explicit ScopedHandleResolver(const HandleResolverScope& scope) noexcept {
			HandleResolver::PushScope(scope);
		}
		~ScopedHandleResolver() {
			HandleResolver::PopScope();
		}

		ScopedHandleResolver(const ScopedHandleResolver&) = delete;
		ScopedHandleResolver& operator=(const ScopedHandleResolver&) = delete;
	};

} // namespace Shalltear
