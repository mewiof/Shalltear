#pragma once

#include "Shalltear/Editor/BuildTarget.h"

namespace Shalltear {

	class LinuxTarget : public IBuildTarget {
	public:
		static constexpr std::string_view TRIPLE = "x86_64-unknown-linux-gnu";

		std::string_view GetId() const override { return "Linux"; }
		std::string_view GetDisplayName() const override { return "Linux"; }

		bool IsAvailable(str& outReason) const override;
		void DescribeSettings(Vector<BuildSettingDesc>& out) const override;
		void Validate(const str& projectPath, const ResolvedProfile& profile, Vector<BuildIssue>& out) const override;
		void ConfigurePipeline(BuildContext& context) const override;
		str GetLaunchCommand(const BuildContext& context) const override;
	};

	class LinuxServerTarget : public LinuxTarget {
	public:
		std::string_view GetId() const override { return "LinuxServer"; }
		std::string_view GetDisplayName() const override { return "Linux Dedicated Server"; }

		void DescribeSettings(Vector<BuildSettingDesc>& out) const override;
		void ConfigurePipeline(BuildContext& context) const override;
	};

} // namespace Shalltear
