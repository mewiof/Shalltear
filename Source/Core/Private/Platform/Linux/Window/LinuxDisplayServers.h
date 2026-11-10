#pragma once

#include "Platform/Linux/Window/LinuxWindow.h"

#include <span>

namespace Shalltear::Linux {

	struct ServerCandidate {
		const char* name;
		// allocated with New<>(MemTag::Platform), null + a reason when unusable
		DisplayServer* (*connect)(str& outWhy);
	};

	// tried in this order
	std::span<const ServerCandidate> GetServerCandidates();

} // namespace Shalltear::Linux
