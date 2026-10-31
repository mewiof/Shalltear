#include "Shalltear/Core/Frame/FrameStages.h"

#include "Shalltear/Core/Log.h"

#include "Module/EngineStateTable.h"
#include "TimeInternal.h"

#include <vector>

namespace Shalltear {

	namespace {

		struct Registration {
			FrameStageFn fn = nullptr;
			void* user = nullptr;
			const void* owner = nullptr;
			FrameStagePosition position = FrameStagePosition::Normal;
		};

		struct FrameStageData {
			std::vector<Registration> stages[FRAME_STAGE_COUNT];
			u32 walkDepth = 0;
			bool hasTombstones = false;
			bool needsSort = false;
		};

		FrameStageData* _boundData = nullptr;

		FrameStageData& GetData() noexcept {
			if (_boundData) {
				return *_boundData;
			}
			static FrameStageData data;
			return data;
		}

		void SettleStages(FrameStageData& data) {
			if (data.walkDepth != 0 || (!data.hasTombstones && !data.needsSort)) {
				return;
			}
			for (std::vector<Registration>& stage : data.stages) {
				if (data.hasTombstones) {
					for (size_t i = stage.size(); i > 0; --i) {
						if (!stage[i - 1].fn) {
							stage.erase(stage.begin() + static_cast<ptrdiff_t>(i - 1));
						}
					}
				}
				if (data.needsSort) {
					for (size_t i = 1; i < stage.size(); ++i) {
						Registration moving = stage[i];
						size_t j = i;
						while (j > 0 && stage[j - 1].position > moving.position) {
							stage[j] = stage[j - 1];
							--j;
						}
						stage[j] = moving;
					}
				}
			}
			data.hasTombstones = false;
			data.needsSort = false;
		}

	} // anonymous namespace

	bool FrameStages::Register(FrameStage stage, FrameStagePosition position, FrameStageFn fn, void* user, const void* owner) {
		u32 index = static_cast<u32>(stage);
		if (index >= FRAME_STAGE_COUNT || !fn) {
			SLT_LOG_WARN("Frame", "FrameStages::Register: ignoring a registration with no function or an unknown stage");
			return false;
		}
		if (!owner) {
			SLT_LOG_WARN("Frame", "FrameStages::Register: ignoring a registration into '{}' with no owner",
						 GetFrameStageName(stage));
			return false;
		}
		FrameStageData& data = GetData();
		data.stages[index].push_back(Registration{fn, user, owner, position});
		data.needsSort = true;
		SettleStages(data);
		return true;
	}

	u32 FrameStages::UnregisterOwnedBy(const void* owner) {
		if (!owner) {
			return 0;
		}
		FrameStageData& data = GetData();
		u32 removed = 0;
		for (std::vector<Registration>& stage : data.stages) {
			for (Registration& entry : stage) {
				if (entry.fn && entry.owner == owner) {
					entry.fn = nullptr;
					data.hasTombstones = true;
					++removed;
				}
			}
		}
		SettleStages(data);
		return removed;
	}

	void FrameStages::Run(FrameStage stage, const FrameStageArgs& args) {
		u32 index = static_cast<u32>(stage);
		if (index >= FRAME_STAGE_COUNT) {
			return;
		}
		TimeInternal::StageScope timeStage(stage);

		FrameStageData& data = GetData();
		struct WalkGuard {
			FrameStageData& data;
			explicit WalkGuard(FrameStageData& d) noexcept : data(d) { ++data.walkDepth; }
			~WalkGuard() {
				--data.walkDepth;
				SettleStages(data);
			}
		} walkGuard{data};
		std::vector<Registration>& registrations = data.stages[index];
		for (size_t i = 0; i < registrations.size(); ++i) {
			Registration entry = registrations[i];
			if (entry.fn) {
				entry.fn(args, entry.user);
			}
		}
	}

} // namespace Shalltear

namespace Shalltear::ModuleInternal {

	void CaptureFrameStagesEngineState(EngineStateTable& table) {
		table.frameStages = &GetData();
	}

	void BindFrameStagesEngineState(const EngineStateTable& table) {
		_boundData = static_cast<FrameStageData*>(table.frameStages);
	}

} // namespace Shalltear::ModuleInternal
