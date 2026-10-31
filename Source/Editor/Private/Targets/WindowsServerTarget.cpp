#include "Targets/WindowsTarget.h"

#include "Shalltear/Editor/BuildPipeline.h"

namespace Shalltear {

	void WindowsServerTarget::ConfigurePipeline(BuildContext& context) const {
		WindowsTarget::ConfigurePipeline(context);

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
