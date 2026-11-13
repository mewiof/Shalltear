#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Module/ModuleEvents.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Network/Socket.h"

#include "NetworkInternal.h"

namespace Shalltear::NetworkImpl {

	namespace {

		class UnloadWatcher : public Object {
		public:
			void OnImageUnloading(const ImageUnloadingEvent&) { Net::HTTPInternal::DropCallbacks(); }
		};

		UnloadWatcher* _watcher = nullptr;

		void OnEndFrame(const FrameStageArgs&, void*) {
			Net::Dispatch();
		}

	} // anonymous namespace

	void RegisterPackage(const void* owner) {
		FrameStages::Register(FrameStage::EndFrame, FrameStagePosition::Early, &OnEndFrame, nullptr, owner);
		if (!_watcher) {
			_watcher = New<UnloadWatcher>(MemTag::Platform);
			ModuleEvents::ImageUnloading.Subscribe(EventBus::GetGlobal(), Handle<UnloadWatcher>(_watcher), &UnloadWatcher::OnImageUnloading);
		}
	}

	// workers run this image's code, must join before DLL detach
	void ShutdownPackage() {
		if (_watcher) {
			EventBus::GetGlobal().UnsubscribeAll(_watcher->GetInstanceId());
			Delete(_watcher);
			_watcher = nullptr;
		}
		Net::Workers::Shutdown();
	}

} // namespace Shalltear::NetworkImpl

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(Network, &Shalltear::NetworkImpl::RegisterPackage, &Shalltear::NetworkImpl::ShutdownPackage)
