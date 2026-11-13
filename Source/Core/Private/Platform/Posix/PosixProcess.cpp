#include "Platform/Process.h"

#include <cstdlib>
#include <string_view>

#include <pwd.h>
#include <time.h>
#include <unistd.h>

namespace Shalltear::Platform {

	str GetUserHomeDirectory() {
		if (const char* home = std::getenv("HOME"); home && *home) {
			return home;
		}
		if (const passwd* entry = ::getpwuid(::getuid()); entry && entry->pw_dir) {
			return entry->pw_dir;
		}
		return {};
	}

	str GetUserLanguage() {
		if (const char* list = std::getenv("LANGUAGE"); list && *list) {
			const std::string_view first = std::string_view(list).substr(0, std::string_view(list).find(':'));
			if (!first.empty()) {
				return str(first);
			}
		}
		for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
			if (const char* value = std::getenv(name); value && *value) {
				return value;
			}
		}
		return {};
	}

	bool AcquireStandardOutput(bool) {
		return true;
	}

	void PrepareConsoleOutput() {
	}

	void ToLocalTime(std::time_t time, std::tm& out) {
		::localtime_r(&time, &out);
	}

} // namespace Shalltear::Platform
