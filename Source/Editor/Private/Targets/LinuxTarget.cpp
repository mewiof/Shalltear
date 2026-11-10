#include "Targets/LinuxTarget.h"

#include "Shalltear/Editor/BuildPipeline.h"

#include "ChildProcess.h"
#include "Platform/EditorPlatform.h"

namespace Shalltear {

	namespace {

		constexpr std::string_view EXECUTABLE_SUFFIX = ".x86_64";

		void AddIssue(Vector<BuildIssue>& out, BuildIssueSeverity severity, const str& profile, const char* key,
					  str message) {
			out.push_back(BuildIssue{severity, profile, str(key), std::move(message)});
		}

		bool IsHost() {
			return EditorPlatform::GetHostPlatformName() == "Linux";
		}

		str MissingSDK(const BuildProfileOps::StaticSDKProbe& probe) {
			if (!probe.fix.empty()) {
				return probe.fix;
			}
			constexpr std::string_view BUILD = "`cmake --preset linux-static-release` -> `cmake --build --preset linux-static-release`.";
			return (IsHost() ? "Run " : "Run `python Scripts/fetch_prebuilt.py --linux` -> ") + str(BUILD);
		}

	} // anonymous namespace

	bool LinuxTarget::IsAvailable(str& outReason) const {
		outReason.clear();
		if (IsHost()) {
			return true;
		}
		BuildProfileOps::StaticSDKProbe probe;
		BuildProfileOps::FindStaticSDKRoot(probe, TRIPLE);
		if (probe.root.empty()) {
			outReason = "no STATIC SDK built for Linux was found. " + MissingSDK(probe);
			return false;
		}
		return true;
	}

	void LinuxTarget::DescribeSettings(Vector<BuildSettingDesc>& out) const {
		BuildSettingDesc symbols;
		symbols.key = "copyDebugSymbols";
		symbols.type = BuildSettingType::Bool;
		symbols.merge = BuildMergePolicy::Replace;
		symbols.label = "Copy Debug Symbols";
		symbols.help = "emit a .debug file beside the executable. A Debug configuration has symbols either way";
		symbols.defaultValue = "false";
		out.push_back(std::move(symbols));
	}

	void LinuxTarget::Validate(const str&, const ResolvedProfile& profile, Vector<BuildIssue>& out) const {
		BuildProfileOps::StaticSDKProbe probe;
		BuildProfileOps::FindStaticSDKRoot(probe, TRIPLE);
		if (probe.root.empty()) {
			AddIssue(out, BuildIssueSeverity::Error, profile.name, "",
					 "a Shalltear player links the game statically + no STATIC SDK built for Linux was found. " + MissingSDK(probe));
		}
	}

	void LinuxTarget::ConfigurePipeline(BuildContext& context) const {
		BuildProfileOps::StaticSDKProbe probe;
		BuildProfileOps::FindStaticSDKRoot(probe, TRIPLE);
		context.sdkRoot = probe.root;
		if (!probe.root.empty()) {
			context.Log("static SDK: " + probe.root + " (" + probe.source + ")");
		}

		context.entrySource = "PlayerMain.cpp";
		context.windowedSubsystem = true;

		context.defines.push_back("SLT_PLAYER");

		if (!IsHost()) {
			context.Log("built on " + str(EditorPlatform::GetHostPlatformName()) +
						" (no executable bit). Run `chmod +x` on the executable on Linux");
		}
	}

	str LinuxTarget::GetLaunchCommand(const BuildContext& context) const {
		if (context.stagingDir.empty() || context.executableName.empty() || !IsHost()) {
			return {};
		}
		return QuoteCommandArgument(context.stagingDir + "/" + context.executableName + str(EXECUTABLE_SUFFIX));
	}

	void LinuxServerTarget::ConfigurePipeline(BuildContext& context) const {
		LinuxTarget::ConfigurePipeline(context);

		context.entrySource = "ServerMain.cpp";
		context.windowedSubsystem = false;
		for (str& define : context.defines) {
			if (define == "SLT_PLAYER") {
				define = "SLT_SERVER";
			}
		}

		const ResolvedSetting* setting = context.profile ? context.profile->Find("executableName") : nullptr;
		if (!setting || setting->value.empty()) {
			context.executableName += "Server";
		}
	}

} // namespace Shalltear
