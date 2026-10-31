#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>
#include <vector>

namespace Shalltear {

	enum class BuildIssueSeverity : u8 {
		Warning,
		Error
	};

	struct BuildIssue {
		BuildIssueSeverity severity = BuildIssueSeverity::Error;
		str profile;
		str key;
		str message;
	};

	enum class BuildSettingType : u8 {
		Bool,
		Int,
		String,
		Path,
		Enum,
		StringList,
		SceneList
	};

	enum class BuildMergePolicy : u8 {
		Replace,
		Union
	};

	struct BuildSettingDesc {
		str key;
		BuildSettingType type = BuildSettingType::String;
		BuildMergePolicy merge = BuildMergePolicy::Replace;
		str label;
		str help;

		str defaultValue;
		Vector<str> defaultItems;
		Vector<str> enumValues;
	};

	class SHALLTEAR_EDITOR_API BuildProfile {
	public:
		str name;

		str inherits;

		str target;

		i32 formatVersion = 1;

		bool Has(std::string_view key) const;
		bool Remove(std::string_view key);

		size_t SettingCount() const { return _settings.size(); }
		std::string_view SettingKeyAt(size_t index) const;

		str GetString(std::string_view key, std::string_view fallback = {}) const;
		void SetString(std::string_view key, std::string_view value);

		bool GetList(std::string_view key, Vector<str>& out) const;
		void SetList(std::string_view key, const Vector<str>& items);
	private:
		struct Setting {
			str key;
			str json;
		};

		const Setting* Find(std::string_view key) const;
		void Set(std::string_view key, str json);

		std::vector<Setting> _settings;

		std::vector<Setting> _extra;

		friend class BuildProfileReader;
		friend class BuildProfileWriter;
	};

	struct ResolvedSetting {
		str key;
		BuildSettingType type = BuildSettingType::String;
		BuildMergePolicy merge = BuildMergePolicy::Replace;

		str value;
		str rawValue;
		Vector<str> items;
		Vector<str> rawItems;
		str source;

		Vector<str> itemSources;
	};

	struct ResolvedProfile {
		str name;
		str target;
		str targetSource;

		Vector<str> chain;

		Vector<ResolvedSetting> settings;

		Vector<BuildIssue> issues;

		const ResolvedSetting* Find(std::string_view key) const {
			for (const ResolvedSetting& setting : settings) {
				if (setting.key == key) {
					return &setting;
				}
			}
			return nullptr;
		}
	};

	namespace BuildProfileOps {

		SHALLTEAR_EDITOR_API void GetCoreSettings(Vector<BuildSettingDesc>& out);

		SHALLTEAR_EDITOR_API str GetProfilesDirectory(const str& projectPath);

		SHALLTEAR_EDITOR_API str GetProfilePath(const str& projectPath, const str& name);

		SHALLTEAR_EDITOR_API bool IsValidProfileName(std::string_view name);

		SHALLTEAR_EDITOR_API bool LoadProfile(const str& filePath, BuildProfile& out);

		SHALLTEAR_EDITOR_API bool SaveProfile(const str& filePath, const BuildProfile& profile);

		SHALLTEAR_EDITOR_API void ListProfiles(const str& projectPath, Vector<str>& out);

		SHALLTEAR_EDITOR_API bool WriteProfileRoster(const str& projectPath, const Vector<str>& names);

		SHALLTEAR_EDITOR_API void ListBuildAllProfiles(const str& projectPath, Vector<str>& out);

		SHALLTEAR_EDITOR_API void GetProjectScenes(const str& projectPath, Vector<str>& out);

		SHALLTEAR_EDITOR_API void ResolveProfile(const str& projectPath, const str& name,
												 const Vector<BuildSettingDesc>& schema, ResolvedProfile& out);

		SHALLTEAR_EDITOR_API void ValidateProfile(const str& projectPath, const ResolvedProfile& profile,
												  const Vector<BuildSettingDesc>& schema, Vector<BuildIssue>& out);

		struct StaticSDKProbe {
			str root;
			str source;
			str fix;
		};

		SHALLTEAR_EDITOR_API void FindStaticSDKRoot(StaticSDKProbe& out);

		SHALLTEAR_EDITOR_API bool MigrateStaticSDKRootToUserScope(const str& projectPath);

		SHALLTEAR_EDITOR_API u32 NextBuildNumber(const str& projectPath, std::string_view targetId);

		SHALLTEAR_EDITOR_API bool CreateProfile(const str& projectPath, const str& name, std::string_view inherits = {});

		SHALLTEAR_EDITOR_API bool DuplicateProfile(const str& projectPath, const str& source, const str& newName);

		SHALLTEAR_EDITOR_API bool DeleteProfile(const str& projectPath, const str& name,
												Vector<str>* outReparented = nullptr);

		SHALLTEAR_EDITOR_API void GetProfileChildren(const str& projectPath, const str& name, Vector<str>& out);

		SHALLTEAR_EDITOR_API bool RenameProfile(const str& projectPath, const str& oldName, const str& newName);

		SHALLTEAR_EDITOR_API bool ExportProfile(const str& projectPath, const str& name, const str& destinationPath);

		SHALLTEAR_EDITOR_API bool ImportProfile(const str& projectPath, const str& sourcePath, str& outName,
												Vector<BuildIssue>& outIssues);

	} // namespace BuildProfileOps

} // namespace Shalltear
