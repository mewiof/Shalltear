#pragma once

#include "Shalltear/Core/Types.h"

#include <ctime>

namespace Shalltear::Platform {

	str GetExecutablePath();

	str GetUserDataDirectory();
	str GetUserHomeDirectory();
	str GetUserPicturesDirectory();

	// raw OS form ("en-US", "ru_RU.UTF-8")
	str GetUserLanguage();

	bool AcquireStandardOutput(bool createConsole);
	void PrepareConsoleOutput();

	void ToLocalTime(std::time_t time, std::tm& out);

} // namespace Shalltear::Platform
