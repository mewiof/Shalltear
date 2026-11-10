#include "Platform/Linux/Window/LinuxDisplayServers.h"

namespace Shalltear::Linux {

	extern const ServerCandidate X11_CANDIDATE;

	std::span<const ServerCandidate> GetServerCandidates() {
		static const ServerCandidate candidates[] = {X11_CANDIDATE};
		return candidates;
	}

} // namespace Shalltear::Linux
