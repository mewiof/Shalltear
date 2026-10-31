#include "Graphs.h"

#include "Shalltear/Core/Module/ModuleAPI.h"

namespace Shalltear {

	void RegisterGraphs(const void* owner) {
		(void)owner;
	}

} // namespace Shalltear

SLT_IMPLEMENT_PACKAGE(Graphs, &Shalltear::RegisterGraphs)
