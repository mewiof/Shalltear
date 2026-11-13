#include "Platform/Process.h"

#include "Platform/XDGUserDirs.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
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

	str GetUserPicturesDirectory() {
		const str home = GetUserHomeDirectory();
		const str config = GetUserDataDirectory();
		str text;
		if (!config.empty()) {
			std::ifstream file(config + "/user-dirs.dirs");
			text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}
		str pictures = ParseXDGUserDir(text, "XDG_PICTURES_DIR", home);
		if (pictures.empty() && !home.empty()) {
			pictures = home + "/Pictures";
		}
		return pictures;
	}

} // namespace Shalltear::Platform
