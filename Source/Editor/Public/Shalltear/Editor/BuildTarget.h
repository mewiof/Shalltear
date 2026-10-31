#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear {

	class BuildContext;

	class SHALLTEAR_EDITOR_API IBuildTarget {
	public:
		virtual ~IBuildTarget() = default;

		virtual std::string_view GetId() const = 0;

		virtual std::string_view GetDisplayName() const = 0;

		virtual bool IsAvailable(str& outReason) const = 0;

		virtual bool RequiresMonotonicBuildNumber() const { return false; }

		virtual void DescribeSettings(Vector<BuildSettingDesc>& out) const = 0;

		virtual void Validate(const str& projectPath, const ResolvedProfile& profile,
							  Vector<BuildIssue>& out) const = 0;

		virtual void ConfigurePipeline(BuildContext& context) const = 0;

		virtual str GetLaunchCommand(const BuildContext& context) const = 0;
	};

	class SHALLTEAR_EDITOR_API BuildTargetRegistry {
	public:
		static void RegisterBuiltIns();
		static void Shutdown();

		static bool Register(const IBuildTarget* target, const void* owner = nullptr);

		static const IBuildTarget* Find(std::string_view id);

		static u32 UnregisterOwnedBy(const void* owner);

		static void GetTargetIds(Vector<str>& out);

		static str GetDefaultTargetId();

		static void GetSchemaFor(std::string_view id, Vector<BuildSettingDesc>& out);
	};

} // namespace Shalltear
