#include "Shalltear/Core/Serialization/HandleResolver.h"

#include "Shalltear/Core/Assert.h"

#include "Module/EngineStateTable.h"

#include <atomic>

namespace Shalltear {

	namespace {

		constexpr u32 MAX_SCOPE_DEPTH = 8;

		struct ResolverTLS {
			HandleResolverScope scopes[MAX_SCOPE_DEPTH];
			u32 depth = 0;
			HandleObserverFn observer = nullptr;
			void* observerUser = nullptr;
		};

		thread_local ResolverTLS _localTLS;

		ResolverTLS* LocalTLS() noexcept { return &_localTLS; }

		struct ResolverHooks {
			std::atomic<HandleToGuidFn> toGuid{nullptr};
			std::atomic<GuidToHandleFn> toHandle{nullptr};

			ResolverTLS* (*tlsFor)() noexcept = nullptr;
		};

		ResolverHooks* _boundHooks = nullptr;

		ResolverHooks& GetHooks() noexcept {
			if (_boundHooks)
				return *_boundHooks;
			static ResolverHooks local;
			return local;
		}

		ResolverTLS& TLS() noexcept {
			if (_boundHooks && _boundHooks->tlsFor) {
				return *_boundHooks->tlsFor();
			}
			return _localTLS;
		}

	} // anonymous namespace

	void HandleResolver::SetHooks(HandleToGuidFn toGuid, GuidToHandleFn toHandle) noexcept {
		auto& hooks = GetHooks();
		hooks.toGuid.store(toGuid, std::memory_order_release);
		hooks.toHandle.store(toHandle, std::memory_order_release);
	}

	void HandleResolver::ResetHooks() noexcept {
		auto& hooks = GetHooks();
		hooks.toGuid.store(nullptr, std::memory_order_release);
		hooks.toHandle.store(nullptr, std::memory_order_release);
	}

	bool HandleResolver::ToGuid(u64 instanceId, Guid* outGuid, StringHash* outPointeeType) noexcept {
		if (outGuid) {
			*outGuid = Guid{};
		}
		if (outPointeeType) {
			*outPointeeType = StringHash();
		}

		ResolverTLS& tls = TLS();
		if (tls.depth > 0) {
			const HandleResolverScope& scope = tls.scopes[tls.depth - 1];
			if (scope.toGuid && scope.toGuid(scope.user, instanceId, outGuid, outPointeeType)) {
				return true;
			}
		}

		HandleToGuidFn hook = GetHooks().toGuid.load(std::memory_order_acquire);
		if (!hook) {
			return false;
		}
		return hook(instanceId, outGuid, outPointeeType);
	}

	void HandleResolver::SetObserver(HandleObserverFn fn, void* user) noexcept {
		ResolverTLS& tls = TLS();
		tls.observer = fn;
		tls.observerUser = user;
	}

	u64 HandleResolver::ToHandle(const Guid& guid, StringHash pointeeType) noexcept {
		ResolverTLS& tls = TLS();
		if (tls.observer) {
			tls.observer(tls.observerUser, guid, pointeeType);
		}

		if (tls.depth > 0) {
			const HandleResolverScope& scope = tls.scopes[tls.depth - 1];
			u64 id = 0;
			if (scope.toHandle && scope.toHandle(scope.user, guid, pointeeType, &id)) {
				return id;
			}
		}

		GuidToHandleFn hook = GetHooks().toHandle.load(std::memory_order_acquire);
		if (!hook) {
			return 0;
		}
		return hook(guid, pointeeType);
	}

	void HandleResolver::PushScope(const HandleResolverScope& scope) noexcept {
		ResolverTLS& tls = TLS();
		SLT_CORE_ASSERT_MSG(tls.depth < MAX_SCOPE_DEPTH, "HandleResolver scope stack overflow");
		if (tls.depth >= MAX_SCOPE_DEPTH) {
			return;
		}
		tls.scopes[tls.depth++] = scope;
	}

	void HandleResolver::PopScope() noexcept {
		ResolverTLS& tls = TLS();
		SLT_CORE_ASSERT_MSG(tls.depth > 0, "HandleResolver::PopScope without a matching PushScope");
		if (tls.depth > 0) {
			--tls.depth;
		}
	}

	u32 HandleResolver::GetScopeDepth() noexcept {
		return TLS().depth;
	}

	namespace ModuleInternal {

		void CaptureHandleResolverEngineState(EngineStateTable& table) {
			ResolverHooks& hooks = GetHooks();
			hooks.tlsFor = &LocalTLS;
			table.handleResolver = &hooks;
		}

		void BindHandleResolverEngineState(const EngineStateTable& table) {
			_boundHooks = static_cast<ResolverHooks*>(table.handleResolver);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
