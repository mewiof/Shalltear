#include "Platform/Process.h"

#include <cstdlib>
#include <vector>

#include <unistd.h>

namespace Shalltear::Platform {

	str GetExecutablePath() {
		std::vector<char> buffer(1024);
		for (;;) {
			const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
			if (length < 0) {
				return {};
			}
			if (static_cast<size_t>(length) < buffer.size()) {
				return str(buffer.data(), static_cast<size_t>(length));
			}
			buffer.resize(buffer.size() * 2);
		}
	}

	str GetUserDataDirectory() {
		if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && config[0] == '/') {
			return config;
		}
		const str home = GetUserHomeDirectory();
		return home.empty() ? str() : home + "/.config";
	}

} // namespace Shalltear::Platform
