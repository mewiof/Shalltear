#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/GraphSearch.h"

#include <functional>

namespace Shalltear::GraphSearcher {

	struct Desc {
		Vector<GraphSearchEntry> library{GetMemoryResource(MemTag::Editor)};
		Vec2 at{};
		std::function<void(const GraphSearchEntry& entry)> onPick;
	};

	UI::ElementHandle Open(UI::Context& context, Desc desc);

} // namespace Shalltear::GraphSearcher
