#pragma once

#include "Shalltear/Core/Types.h"

namespace Shalltear::Physics3DEditor {

	void RegisterColliderGizmos(const void* owner);
	void RegisterPhysicsDebugView(const void* owner);
	void RegisterCategoryDrawers(const void* owner);
	void RegisterComponentIcons(const void* owner);

	void RegisterHeldBodies(const void* owner);
	void ReleaseHeldBodies();

	void RegisterAssetBadges(const void* owner);

	void RegisterEditorExtensions(const void* owner);

} // namespace Shalltear::Physics3DEditor
