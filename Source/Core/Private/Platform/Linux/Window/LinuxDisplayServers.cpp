#include "Platform/Linux/Window/LinuxDisplayServers.h"

namespace Shalltear::Linux {

	extern const ServerCandidate X11Candidate;

	std::span<const ServerCandidate> GetServerCandidates() {
		static const ServerCandidate candidates[] = {X11Candidate};
		return candidates;
	}

} // namespace Shalltear::Linux
