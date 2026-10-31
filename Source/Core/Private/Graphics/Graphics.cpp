#include "Shalltear/Core/Graphics/Graphics.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Settings.h"

#include "Graphics/GraphicsSettings.h"

namespace Shalltear::Graphics {

	namespace {

		GraphicsSettings* Category() {
			return Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
		}

		void SplitProfiles(std::string_view packed, Vector<Guid>& out) {
			out.clear();
			for (size_t start = 0; start <= packed.size();) {
				const size_t separator = packed.find(';', start);
				const size_t end = separator == std::string_view::npos ? packed.size() : separator;
				const std::string_view entry = packed.substr(start, end - start);
				if (!entry.empty()) {
					out.push_back(Guid::FromString(entry));
				}
				if (separator == std::string_view::npos) {
					break;
				}
				start = separator + 1;
			}
		}

	} // anonymous namespace

	u32 GetQualityLevelCount() {
		const GraphicsSettings* settings = Category();
		if (!settings || settings->qualityProfiles.empty()) {
			return 0;
		}
		Vector<Guid> ids{GetMemoryResource(MemTag::Graphics)};
		SplitProfiles(settings->qualityProfiles, ids);
		return static_cast<u32>(ids.size());
	}

	i32 GetQualityLevel() {
		const GraphicsSettings* settings = Category();
		if (!settings) {
			return -1;
		}
		const i32 level = settings->qualityLevel;
		return (level >= 0 && static_cast<u32>(level) < GetQualityLevelCount()) ? level : -1;
	}

	bool SetQualityLevel(i32 level) {
		GraphicsSettings* settings = Category();
		if (!settings) {
			SLT_LOG_WARN("Graphics", "SetQualityLevel: the Graphics settings category isn't declared");
			return false;
		}
		const u32 count = GetQualityLevelCount();
		if (level < -1 || (level >= 0 && static_cast<u32>(level) >= count)) {
			SLT_LOG_WARN("Graphics", "SetQualityLevel({}) is outside the {} level(s) this project declares", level, count);
			return false;
		}
		if (settings->qualityLevel == level) {
			return true;
		}
		Settings::SetFieldEditScope("Graphics", "qualityLevel", SettingScope::UserProject);
		settings->qualityLevel = level;
		Settings::NotifyChanged("Graphics", "qualityLevel");
		Settings::Save();
		return true;
	}

	void GetQualityProfileIds(Vector<Guid>& out) {
		out.clear();
		const GraphicsSettings* settings = Category();
		if (!settings) {
			return;
		}
		SplitProfiles(settings->qualityProfiles, out);
	}

} // namespace Shalltear::Graphics

SLT_REGISTER_TYPE(Shalltear::GraphicsSettings)
