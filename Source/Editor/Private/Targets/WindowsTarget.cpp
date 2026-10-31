#include "Targets/WindowsTarget.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/ScriptProjectOps.h"

#include "ChildProcess.h"

namespace Shalltear {

	namespace {

		void AddIssue(Vector<BuildIssue>& out, BuildIssueSeverity severity, const str& profile, const char* key,
					  str message) {
			out.push_back(BuildIssue{severity, profile, str(key), std::move(message)});
		}

	} // anonymous namespace

	bool WindowsTarget::IsAvailable(str& outReason) const {
#if defined(_WIN32)
		outReason.clear();
		return true;
#else
		outReason = "a Windows player can only be built on Windows";
		return false;
#endif
	}

	void WindowsTarget::DescribeSettings(Vector<BuildSettingDesc>& out) const {
		BuildSettingDesc symbols;
		symbols.key = "copyDebugSymbols";
		symbols.type = BuildSettingType::Bool;
		symbols.merge = BuildMergePolicy::Replace;
		symbols.label = "Copy Debug Symbols";
		symbols.help = "emit a .pdb beside the executable. A Debug configuration produces one either way";
		symbols.defaultValue = "false";
		out.push_back(std::move(symbols));
	}

	void WindowsTarget::Validate(const str&, const ResolvedProfile& profile, Vector<BuildIssue>& out) const {
		BuildProfileOps::StaticSDKProbe probe;
		BuildProfileOps::FindStaticSDKRoot(probe);
		if (probe.root.empty()) {
			AddIssue(out, BuildIssueSeverity::Error, profile.name, "",
					 "a Shalltear player links the game statically + no STATIC SDK was found. " + probe.fix);
		}
	}

	void WindowsTarget::ConfigurePipeline(BuildContext& context) const {
		BuildProfileOps::StaticSDKProbe probe;
		BuildProfileOps::FindStaticSDKRoot(probe);
		context.sdkRoot = probe.root;
		if (!probe.root.empty()) {
			context.Log("static SDK: " + probe.root + " (" + probe.source + ")");
		}

		context.entrySource = "PlayerMain.cpp";
		context.windowedSubsystem = true;

		context.defines.push_back("SLT_PLAYER");
	}

	str WindowsTarget::GetLaunchCommand(const BuildContext& context) const {
		if (context.stagingDir.empty() || context.executableName.empty()) {
			return {};
		}
		return QuoteCommandArgument(context.stagingDir + "/" + context.executableName + ".exe");
	}

} // namespace Shalltear
