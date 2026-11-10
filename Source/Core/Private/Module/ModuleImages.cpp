#include "Shalltear/Core/Module/ModuleSystem.h"

#include "Platform/Library.h"

namespace Shalltear {

	void* ModuleSystem::GetExecutableImage() {
		return Platform::GetExecutableImage();
	}

	void* ModuleSystem::FindSymbol(const void* imageBase, const char* name) {
		return Platform::FindSymbol(imageBase, name);
	}

} // namespace Shalltear
