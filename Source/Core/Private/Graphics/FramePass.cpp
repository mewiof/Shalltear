#include "Shalltear/Core/Graphics/FramePass.h"

#include "Graphics/FramePassInternal.h"
#include "Module/EngineStateTable.h"
#include "Shalltear/Core/Log.h"

#include <algorithm>
#include <vector>

namespace Shalltear {

	namespace {

		struct Registration {
			FramePass* pass = nullptr;
			const void* owner = nullptr;
		};

		struct FramePassData {
			std::vector<Registration> passes;
		};

		FramePassData* _boundData = nullptr;

		FramePassData& GetData() noexcept {
			if (_boundData) {
				return *_boundData;
			}
			static FramePassData data;
			return data;
		}

	} // anonymous namespace

	FramePass::~FramePass() = default;

	bool FramePasses::Register(FramePass& pass, const void* owner) {
		if (!owner) {
			SLT_LOG_WARN("Graphics", "FramePasses::Register: ignoring a pass with no owner");
			return false;
		}
		FramePassData& data = GetData();
		for (const Registration& entry : data.passes) {
			if (entry.pass == &pass) {
				SLT_LOG_WARN("Graphics", "FramePasses::Register: ignoring a pass registered twice");
				return false;
			}
		}
		data.passes.push_back({&pass, owner});
		return true;
	}

	u32 FramePasses::UnregisterOwnedBy(const void* owner) {
		if (!owner) {
			return 0;
		}
		std::vector<Registration>& passes = GetData().passes;
		const size_t before = passes.size();
		std::erase_if(passes, [owner](const Registration& entry) { return entry.owner == owner; });
		return static_cast<u32>(before - passes.size());
	}

	namespace GraphicsInternal {

		u32 GetFramePassCount() {
			return static_cast<u32>(GetData().passes.size());
		}

		FramePass* GetFramePass(u32 index) {
			const std::vector<Registration>& passes = GetData().passes;
			return index < passes.size() ? passes[index].pass : nullptr;
		}

	} // namespace GraphicsInternal

	namespace ModuleInternal {

		void CaptureFramePassesEngineState(EngineStateTable& table) {
			table.framePasses = &GetData();
		}

		void BindFramePassesEngineState(const EngineStateTable& table) {
			_boundData = static_cast<FramePassData*>(table.framePasses);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
