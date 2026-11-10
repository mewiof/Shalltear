#include "Platform/Process.h"

#include <cstdlib>

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

	bool AcquireStandardOutput(bool) {
		return true;
	}

	void PrepareConsoleOutput() {
	}

	void ToLocalTime(std::time_t time, std::tm& out) {
		::localtime_r(&time, &out);
	}

} // namespace Shalltear::Platform
