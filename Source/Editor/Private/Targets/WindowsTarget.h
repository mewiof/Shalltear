#pragma once

#include "Shalltear/Editor/BuildTarget.h"

namespace Shalltear {

	class WindowsTarget : public IBuildTarget {
	public:
		std::string_view GetId() const override { return "Windows"; }
		std::string_view GetDisplayName() const override { return "Windows"; }

		bool IsAvailable(str& outReason) const override;
		void DescribeSettings(Vector<BuildSettingDesc>& out) const override;
		void Validate(const str& projectPath, const ResolvedProfile& profile, Vector<BuildIssue>& out) const override;
		void ConfigurePipeline(BuildContext& context) const override;
		str GetLaunchCommand(const BuildContext& context) const override;
	};

	class WindowsServerTarget : public WindowsTarget {
	public:
		std::string_view GetId() const override { return "WindowsServer"; }
		std::string_view GetDisplayName() const override { return "Windows Dedicated Server"; }

		void ConfigurePipeline(BuildContext& context) const override;
	};

} // namespace Shalltear
