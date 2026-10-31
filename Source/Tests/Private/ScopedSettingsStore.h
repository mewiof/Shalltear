#pragma once

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Settings.h"

#include <cstdlib>
#include <filesystem>

namespace Shalltear {

	class ScopedUserDataDirectory {
	public:
		explicit ScopedUserDataDirectory(const std::filesystem::path& directory) {
			std::error_code ec;
			std::filesystem::create_directories(directory, ec);
			char* previous = nullptr;
			size_t length = 0;
			if (::_dupenv_s(&previous, &length, "LOCALAPPDATA") == 0 && previous) {
				_previous = previous;
				_had = true;
			}
			std::free(previous);
			::_putenv_s("LOCALAPPDATA", directory.string().c_str());
		}

		~ScopedUserDataDirectory() { ::_putenv_s("LOCALAPPDATA", _had ? _previous.c_str() : ""); }

		ScopedUserDataDirectory(const ScopedUserDataDirectory&) = delete;
		ScopedUserDataDirectory& operator=(const ScopedUserDataDirectory&) = delete;
	private:
		str _previous;
		bool _had = false;
	};

	class ScopedSettingsStore {
	public:
		ScopedSettingsStore(const std::filesystem::path& directory, std::string_view userDocument = {}) {
			std::error_code ec;
			std::filesystem::create_directories(directory, ec);
			_paths.user = Path((directory / "UserSettings.ini").generic_string());
			_paths.project = Path((directory / "ProjectSettings.ini").generic_string());
			_paths.userProject = Path((directory / "UserProjectSettings.ini").generic_string());
			if (!userDocument.empty()) {
				FileSystem::WriteTextFile(_paths.user, str(userDocument));
			}
			Settings::Initialize(_paths);
		}

		~ScopedSettingsStore() { Settings::Terminate(); }

		ScopedSettingsStore(const ScopedSettingsStore&) = delete;
		ScopedSettingsStore& operator=(const ScopedSettingsStore&) = delete;

		const SettingsPaths& GetPaths() const { return _paths; }
	private:
		SettingsPaths _paths;
	};

} // namespace Shalltear
