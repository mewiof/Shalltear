#pragma once

#include "Shalltear/Core/Types.h"

#include <ctime>

namespace Shalltear::Platform {

	str GetExecutablePath();

	str GetUserDataDirectory();
	str GetUserHomeDirectory();

	bool AcquireStandardOutput(bool createConsole);
	void PrepareConsoleOutput();

	void ToLocalTime(std::time_t time, std::tm& out);

} // namespace Shalltear::Platform
