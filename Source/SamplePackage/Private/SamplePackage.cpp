#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Types.h"

#include "SamplePackageBehavior.h"

namespace SamplePackageImpl {

	using namespace Shalltear;

	constexpr const char* PACKAGE_MARKER = "SHALLTEAR_PACKAGE_PROBE";

	struct StageProbe {
		u32 runs = 0;
	};

	StageProbe _stageProbe;

	void OnUpdateStage(const FrameStageArgs&, void* user) {
		auto* probe = static_cast<StageProbe*>(user);
		++probe->runs;

		const TypeInfo* behaviorType = TypeRegistry::FindType(StringHash("SamplePackageBehavior"));

		SLT_LOG_INFO("SamplePackage", "{} runs={} dt={:.4f} type={}",
					 PACKAGE_MARKER, probe->runs, Time::GetDeltaTime(),
					 behaviorType ? "registered" : "MISSING");
	}

	void RegisterPackage(const void* owner) {
		_stageProbe = StageProbe{};

		FrameStages::Register(FrameStage::Update, FrameStagePosition::Normal, &OnUpdateStage, &_stageProbe, owner);

		SamplePackageBehavior::GetReflectType();
	}

} // namespace SamplePackageImpl

SLT_IMPLEMENT_PACKAGE(SamplePackage, &SamplePackageImpl::RegisterPackage)
