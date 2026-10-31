#pragma once

#include "GraphNodeLibrary.h"

#include <string_view>

namespace Shalltear {

	enum class GraphSpace : u8 {
		Object,
		View,
		World,
		Tangent,
		AbsoluteWorld,
		Screen
	};

	enum class GraphConversion : u8 {
		Position,
		Direction,
		Normal
	};

	namespace GraphSpaces {

		inline constexpr std::string_view NAMES[] = {"Object", "View", "World", "Tangent", "AbsoluteWorld", "Screen"};
		inline constexpr std::string_view CONVERSIONS[] = {"Position", "Direction", "Normal"};

		GraphValueId Transform(GraphBodyContext& context, GraphValueId in, GraphSpace from, GraphSpace to, GraphConversion type,
							   bool normalize);

		GraphValueId SafeNormalize(GraphProgramBuilder& builder, GraphValueId v);

	} // namespace GraphSpaces

} // namespace Shalltear
