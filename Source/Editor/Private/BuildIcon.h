#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"

namespace Shalltear::BuildIcon {

	void DescribeSetting(Vector<BuildSettingDesc>& out);

	void Validate(const str& projectPath, const ResolvedProfile& profile, Vector<BuildIssue>& out);

	BuildStepStatus Step(BuildContext& context);

} // namespace Shalltear::BuildIcon
