#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	struct TypeInfo;
	struct FieldInfo;

	enum class SettingScope : u8 {
		UserProject = 0,
		User = 1,
		Project = 2,
	};

	inline constexpr u32 SETTING_SCOPE_COUNT = 3;

	enum class MutabilityClass : u8 {
		Hot = 0,
		FrameBoundary = 1,
		DeviceIdle = 2,
		Restart = 3,
	};

	struct SettingsPaths {
		Path userProject;
		Path user;
		Path project;

		str projectText;
	};

	struct SettingsCategoryInfo {
		std::string_view path;
		const TypeInfo* type = nullptr;
		void* live = nullptr;
		SettingScope scope = SettingScope::Project;
		bool overridable = false;
	};

	struct SettingFieldState {
		bool isOverride = false;
		SettingScope resolvedFrom = SettingScope::Project;
		SettingScope editsLandIn = SettingScope::Project;
	};

	class SHALLTEAR_CORE_API Settings {
	public:
		static SettingsPaths GetDefaultPaths();

		static void Initialize(const SettingsPaths& paths);
		static void Terminate();

		static void* DeclareCategory(std::string_view path, const TypeInfo* type, SettingScope scope, bool overridable);

		template <typename T>
		static T* DeclareCategory(std::string_view path, SettingScope scope, bool overridable = false) {
			return static_cast<T*>(DeclareCategory(path, T::GetReflectType(), scope, overridable));
		}

		// must run while the declaring image is still mapped
		static u32 UnregisterOwnedBy(const void* imageBase);

		static void GetCategories(Vector<SettingsCategoryInfo>& out);

		static bool GetFieldState(std::string_view categoryPath, std::string_view fieldName, SettingFieldState& out);

		static bool GetFieldMutability(const FieldInfo* field, MutabilityClass& out);

		static bool SetFieldEditScope(std::string_view categoryPath, std::string_view fieldName, SettingScope scope);

		static void NotifyChanged(std::string_view categoryPath, std::string_view fieldName);

		static bool Save();

		static bool RevertField(std::string_view categoryPath, std::string_view fieldName);

		static bool TryGetStoredValue(std::string_view categoryPath, std::string_view fieldName, str& out);
	};

	struct SettingChangedEvent {
		std::string_view categoryPath;
		std::string_view fieldName;
	};

	namespace SettingsEvents {
		inline constexpr Event<SettingChangedEvent> Changed{"Settings.Changed"_hash};
	}

} // namespace Shalltear
